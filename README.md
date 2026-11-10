# cortex-m4-rtos

A pre-emptive multithreading kernel written from scratch in C for the ARM Cortex-M4, running on an STM32 Nucleo-F401RE. Not a port or configuration of an existing RTOS.

## Features
- **Threads**: up to 16, each with a 1 KB stack carved from a 16 KB pool
- **Dual stacks**: kernel on MSP, threads on PSP
- **Thread launch**: initial exception stack frame built by hand so the CPU "returns" into a new thread
- **System calls**: SVC-based API (`osKernelStart`, `osYield`, thread creation)
- **Scheduler**: round-robin, driven by a 1 ms SysTick; context switch runs in PendSV at the lowest priority so a system call is never interrupted mid-switch
- **Mixed scheduling**: cooperative threads (yield voluntarily) and pre-emptive threads (fixed timeslice) in the same system

## How a context switch works
1. SysTick decrements the running thread's timeslice and pends PendSV when it reaches zero.
2. PendSV saves R4 to R11 on the outgoing thread's stack and stores its PSP.
3. The scheduler picks the next thread and reloads its timeslice.
4. PendSV restores R4 to R11 from the incoming thread and returns on its PSP.

## Demo
A 200 ms pre-emptive worker updates a shared counter while a cooperative thread prints it over UART.

## Build
[STM32CubeIDE project: open and build / or your Makefile steps]

## What I learned
- **A thread is a saved stack frame.** To launch one, I build the frame the CPU expects to restore on exception return. Leaving the Thumb bit out of xPSR faults on the first instruction.
- **Why the context switch lives in PendSV.** Running it at the lowest priority means it never switches threads in the middle of another interrupt, and a system call can't be cut off half-finished.
- **How a system call finds its number.** The SVC handler reads the stacked PC and looks back at the instruction that triggered it.
- **Shared state needs care even with two threads.** The counter shared between the pre-emptive worker and the cooperative printer has to be `volatile`, or the compiler assumes it never changes.

## Credits
Started as lab work for MTE 241 (University of Waterloo). All kernel C code is mine. The low-level assembly routines were provided by the course [and are not included / and are marked as such].