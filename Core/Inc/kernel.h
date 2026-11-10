/*
 * kernel.h
 *
 *  Header for kernel.c
 *  Contains the definitions of the structs, globals and functions that the
 *  kernel uses. User code (threads, main) should NOT go here.
 */

#ifndef INC_KERNEL_H_
#define INC_KERNEL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Kernel defines ------------------------------------------------------------*/
#define RUN_FIRST_THREAD 0x5
#define YIELD 0x2                  /* SVC number for a thread giving up the CPU */

/* Size of each thread's stack, in bytes. MUST be divisible by 8, because the
   ARM Cortex-M requires stacks to be 8-byte aligned. */
#define STACK_SIZE 0x400

/* Total size of the global stack pool, in bytes. MUST match _Min_Stack_Size
   in the linker script (STM32F401RETX_RAM.ld). */
#define STACK_POOL_SIZE 0x4000

/* Maximum number of threads = how many stacks fit in the pool. */
#define MAX_THREADS (STACK_POOL_SIZE / STACK_SIZE)

/* The default timeslice handed to a thread created with osCreateThread, measured
   in SysTick interrupts. The HAL configures SysTick to fire every 1 ms, so this
   is also the value in milliseconds. That 1 ms is our OS's clock speed and
   resolution: we cannot switch faster, or with finer granularity, than this.

   Commercial RTOSs call this the "Round Robin Timeout" and typically use ~5 ms.
   A thread that needs something other than the default is created with
   osCreateThreadWithDeadline instead. */
#define ROUND_ROBIN_TIMEOUT 5

/* System handler priority registers, used to make SVC higher priority than
   PendSV so context switching is not corrupted. */
#define SHPR2 *(uint32_t*)0xE000ED1C //for setting SVC priority, bits 31-24
#define SHPR3 *(uint32_t*)0xE000ED20 // PendSV is bits 23-16
#define _ICSR *(uint32_t*)0xE000ED04 //This lets us trigger PendSV

/* Kernel types --------------------------------------------------------------*/
/* Metadata the OS keeps about a thread. Will be expanded (timing, resource
   permissions, ...) in later labs, and turned into an array in lab 4. */
typedef struct k_thread{
	uint32_t *sp;                       /* the thread's stack pointer */
	void (*thread_function)(void*);     /* the function the thread runs */
	void *args;                         /* the argument handed to that function */
	uint32_t timeslice;                 /* ticks this thread gets per turn */
	uint32_t runtime;                   /* ticks left in the current turn */
}thread;

/* Kernel global variables ---------------------------------------------------*/
/* Pointer to the stack of the thread the kernel is about to run */
extern uint32_t *stackptr;

/* The OS's table of all created threads. */
extern thread osThreads[MAX_THREADS];
extern int threadCount;      /* how many threads have been created */
extern int currentThread;    /* index of the thread currently running */

/* False until the kernel has actually switched to PSP and started thread 0.
   SysTick_Handler must not try to switch threads before that point. */
extern bool kernelRunning;

/* Assembly kernel routines (defined in asmDump.s) ---------------------------*/
extern void runFirstThread(void);

/* Kernel functions ----------------------------------------------------------*/
void SVC_Handler_Main(unsigned int *svc_args);
void osKernelInitialize(void);     /* init kernel globals (stack info, thread struct) */
uint32_t *allocate_stack(void);    /* carve one new stack out of the pool */
/* Make a thread with the default round-robin timeout. args is handed to
   thread_function as its single parameter; pass NULL if the thread does not
   need one. true=ok, false=fail. */
bool osCreateThread(void (*thread_function)(void*), void *args);

/* Same, but the caller chooses how many milliseconds the thread may run before
   the OS pre-empts it. */
bool osCreateThreadWithDeadline(void (*thread_function)(void*), void *args,
                                uint32_t deadline);
void osKernelStart(void);          /* run the created thread (SVC wrapper) */
void osSched(void);                /* pick the next thread (called from PendSV) */

/* Kernel API (SVC wrappers called by user code) -----------------------------*/
void print_success(void);
void print_fail(void);
void run_first_thread(void);
void osYield(void);                 /* thread voluntarily gives up the CPU */

#ifdef __cplusplus
}
#endif

#endif /* INC_KERNEL_H_ */
