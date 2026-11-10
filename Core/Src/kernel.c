/*
 * kernel.c
 *
 *  Contains all of the global variables and functions that the OS needs at
 *  the kernel level. User code (threads, main) lives in main.c instead.
 */

/* Includes ------------------------------------------------------------------*/
#include "kernel.h"
#include "main.h"      /* for CMSIS intrinsics (__asm, __set_PSP) */
#include <stdio.h>

/* Kernel global variables ---------------------------------------------------*/
uint32_t *stackptr;

/* Stack-pool bookkeeping.
 *   msp_init   = the top of the whole stack pool (the initial MSP value).
 *   next_stack = the top of the NEXT stack we will hand out. It walks
 *                downward through the pool by STACK_SIZE on every allocation. */
uint32_t *msp_init;
uint32_t *next_stack;

/* The OS's table of all created threads. */
thread osThreads[MAX_THREADS];
int threadCount   = 0;   /* how many threads have been created */
int currentThread = 0;   /* index of the thread currently running */

/* SysTick starts ticking inside HAL_Init(), long before our threads exist. Until
 * the kernel has actually switched to PSP there is no thread stack for PendSV to
 * save into, so SysTick_Handler must not switch threads. This flag is set at the
 * one moment that becomes true: inside the RUN_FIRST_THREAD system call, right
 * after PSP is loaded. SVC outranks PendSV, so no switch can slip in before it
 * is set. Read by SysTick_Handler in stm32f4xx_it.c, so NOT static. */
bool kernelRunning = false;

/* Kernel API (SVC wrappers) -------------------------------------------------*/
void print_success(void)
{
	__asm("SVC #0");
}

void print_fail(void)
{
	__asm("SVC #1");
}

void run_first_thread(void)
{
	__asm("SVC #5");
}

void osYield(void)
{
	__asm("SVC #2");   /* YIELD: triggers the context switch */
}

/* Kernel initialization -----------------------------------------------------*/
/**
  * @brief Initialize the kernel's global state (stack pool, thread struct).
  *        The user must call this once, before creating any threads.
  */
void osKernelInitialize(void)
{
	/* The very first word of the vector table (address 0x0) holds the initial
	 * MSP value: the top of RAM, and therefore the top of our stack pool. The
	 * hardware guarantees it is 8-byte aligned. */
	msp_init   = *(uint32_t**)0x0;
	next_stack = msp_init;

	/* Start with no threads created and none running. */
	threadCount   = 0;
	currentThread = 0;

	//set the priority of PendSV to almost the weakest
	SHPR3 |= 0xFE << 16; //shift the constant 0xFE 16 bits to set PendSV priority
	SHPR2 |= 0xFDU << 24; //Set the priority of SVC higher than PendSV
}

/* Kernel start --------------------------------------------------------------*/
/**
  * @brief Start running the thread that was created with osCreateThread.
  *        This is just a wrapper around the RUN_FIRST_THREAD system call.
  */
void osKernelStart(void)
{
	currentThread = 0;    /* the first thread to run is index 0 */
	run_first_thread();   /* fires SVC #5 -> SVC_Handler_Main */
}

/* Scheduler -----------------------------------------------------------------*/
/**
  * @brief Decide which thread runs next. Called from PendSV_Handler during a
  *        context switch. Empty for now; the round-robin logic goes here.
  */
void osSched(void)
{
	/* 1. Save the current thread's stack pointer into its struct. Subtract
	 *    8*4 bytes to leave room for R4-R11, which PendSV pushes for us. */
	osThreads[currentThread].sp = (uint32_t*)(__get_PSP() - 8*4);

	/* 2. Round-robin: advance to the next thread, wrapping around at the end. */
	currentThread = (currentThread + 1) % threadCount;

	/* 3. Give the incoming thread a full timeslice. This happens on EVERY
	 *    switch, no matter what caused it. That is what lets cooperative and
	 *    pre-emptive threads share the CPU: a thread that yields early does not
	 *    hand its unused ticks to the next thread, and a thread that gets
	 *    pre-empted does not start its next turn already expired. */
	osThreads[currentThread].runtime = osThreads[currentThread].timeslice;

	/* 4. Point PSP at the new current thread's stack. */
	__set_PSP((uint32_t)osThreads[currentThread].sp);
}

/* The OS clock itself lives in SysTick_Handler, in stm32f4xx_it.c. */

/* Stack allocator -----------------------------------------------------------*/
/**
  * @brief  Carve one new STACK_SIZE-byte stack out of the global stack pool.
  * @retval Pointer to the TOP of the new stack, or NULL if the pool is full.
  * @note   This ONLY finds a location and updates the bookkeeping. It does NOT
  *         set up the thread's stack frame — that happens elsewhere.
  */
uint32_t *allocate_stack(void)
{
	/* Each new stack sits STACK_SIZE below the previous one. */
	uint32_t *new_top = (uint32_t*)((uint32_t)next_stack - STACK_SIZE);

	/* If that would fall past the bottom of the pool, we are out of space. */
	if ((uint32_t)new_top < (uint32_t)msp_init - STACK_POOL_SIZE) {
		return NULL;
	}

	next_stack = new_top;
	return new_top;
}

/* Thread creation -----------------------------------------------------------*/
/**
  * @brief  Create a thread: allocate its stack, build its initial (fake)
  *         exception stack frame, and record it in the thread struct.
  * @param  thread_function  the function the thread will run.
  * @param  args             single argument handed to that function. Under the
  *                          ARM ABI the first argument arrives in R0, so this
  *                          is written into the R0 slot of the fake frame.
  *                          Pass NULL if the thread does not need one.
  * @param  deadline         how many milliseconds this thread may run before the
  *                          OS pre-empts it. Becomes both its timeslice (the
  *                          setting, never changed again) and its runtime (the
  *                          countdown, reloaded from timeslice on every switch).
  *                          Pass 0 for a purely cooperative thread.
  * @retval true on success, false if no stack space was available.
  *
  * @note   This is the real implementation. osCreateThread below is a wrapper
  *         that supplies the default deadline, so the stack-frame construction
  *         lives in exactly one place.
  */
bool osCreateThreadWithDeadline(void (*thread_function)(void*), void *args,
                                uint32_t deadline)
{
	/* 0. Is there room in our thread table? */
	if (threadCount >= MAX_THREADS) {
		return false;
	}

	/* 1. Ask the allocator for stack space. Bail out if the pool is full. */
	uint32_t *sp = allocate_stack();
	if (sp == NULL) {
		return false;
	}

	/* 2. Build the initial stack frame the CPU will "restore" to launch the
	 *    thread (see run_first_thread). Pushed from the top, downward, in the
	 *    exact order the hardware expects:
	 *        xPSR, PC, LR, R12, R3, R2, R1, R0, then R11..R4.
	 *    The first eight are the "auto-stacked" frame the CPU unstacks by
	 *    itself on exception return; R11..R4 are the ones our PendSV handler
	 *    saves and restores manually. */
	*(--sp) = 1 << 24;                    /* xPSR: only the Thumb bit set   */
	*(--sp) = (uint32_t)thread_function;  /* PC:   where the thread starts  */
	*(--sp) = 0xA;                        /* LR                             */
	*(--sp) = 0xA;                        /* R12                            */
	*(--sp) = 0xA;                        /* R3                             */
	*(--sp) = 0xA;                        /* R2                             */
	*(--sp) = 0xA;                        /* R1                             */
	*(--sp) = (uint32_t)args;             /* R0: the thread's argument      */
	for (int i = 0; i < 8; i++) {
		*(--sp) = 0xA;                    /* R11-R4: dummy values           */
	}

	/* 3. Record this thread in the next free slot of the table. */
	osThreads[threadCount].thread_function = thread_function;
	osThreads[threadCount].args            = args;
	osThreads[threadCount].sp              = sp;   /* top of the built frame */
	osThreads[threadCount].timeslice       = deadline;  /* the setting  */
	osThreads[threadCount].runtime         = deadline;  /* the countdown */
	threadCount++;

	return true;
}

/**
  * @brief  Create a thread using the OS's default round-robin timeout.
  * @param  thread_function  the function the thread will run.
  * @param  args             single argument handed to that function, or NULL.
  * @retval true on success, false if no stack space was available.
  */
bool osCreateThread(void (*thread_function)(void*), void *args)
{
	return osCreateThreadWithDeadline(thread_function, args, ROUND_ROBIN_TIMEOUT);
}

/* Kernel functions ----------------------------------------------------------*/
/**
  * @brief Main handler for System service calls (SVC), dispatched from the
  *        assembly SVC_Handler stub.
  */
void SVC_Handler_Main( unsigned int *svc_args )
{
	unsigned int svc_number;
	/*
	 * Stack contains:
	 * r0, r1, r2, r3, r12, r14, the return address and xPSR
	 * First argument (r0) is svc_args[0]
	 */
	svc_number = ( ( char * )svc_args[ 6 ] )[ -2 ] ;
	switch( svc_number ){
	 	 case 0:
	 		 printf("Success!\r\n");
	            break;
	     case 1:
	    	 printf("Fail\r\n");
	    	 break;
	     case YIELD:
	    	 //Pend an interrupt to do the context switch
	    	 _ICSR |= 1<<28;
	    	 __asm("isb");
	    	 break;
		 case RUN_FIRST_THREAD:
			/* Run the currently-selected thread: set PSP to its saved stack
			 * pointer, then switch. */
			__set_PSP((uint32_t)osThreads[currentThread].sp);
			osThreads[currentThread].runtime = osThreads[currentThread].timeslice;
			kernelRunning = true;   /* PSP is valid now: the clock may switch us */
			runFirstThread();
			break;
		default: /* unknown SVC */
			break;
	}
}
