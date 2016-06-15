// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <debug.h>
#include <trace.h>
#include <assert.h>
#include <kernel/thread.h>
#include <arch/arm.h>
#include <arch/arm/cm.h>

#define LOCAL_TRACE 0

struct arm_cm_context_switch_frame {
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t lr;
};

/* since we're implicitly uniprocessor, store a pointer to the current thread here */
thread_t *_current_thread;

void arch_thread_initialize(struct thread *t, vaddr_t entry_point)
{
    LTRACEF("thread %p, stack %p\n", t, t->stack);

    /* find the top of the stack and align it on an 8 byte boundary */
    uint32_t *sp = (void *)ROUNDDOWN((vaddr_t)t->stack + t->stack_size, 8);

    struct arm_cm_context_switch_frame *frame = (void *)sp;
    frame--;

    /* arrange for lr to point to our starting routine */
    frame->lr = (uint32_t)entry_point;

    t->arch.sp = (addr_t)frame;
    t->arch.was_preempted = false;
}

volatile struct arm_cm_exception_frame_long *preempt_frame;

static void pendsv(struct arm_cm_exception_frame_long *frame)
{
    arch_disable_ints();

    LTRACEF("preempting thread %p (%s)\n", _current_thread, _current_thread->name);

    /* save the iframe the pendsv fired on and hit the preemption code */
    preempt_frame = frame;
    thread_preempt();

    LTRACEF("fell through\n");

    /* if we got here, there wasn't anything to switch to, so just fall through and exit */
    preempt_frame = NULL;

    arch_enable_ints();
}

/*
 * raw pendsv exception handler, triggered by interrupt glue to schedule
 * a preemption check.
 */
__NAKED void _pendsv(void)
{
    __asm__ volatile(
#if       (__CORTEX_M >= 0x03)

        "push	{ r4-r11, lr };"
        "mov	r0, sp;"
        "bl		%0;"
        "pop	{ r4-r11, lr };"
        "bx		lr;"
#else
        "push   { lr };"
        "mov    r0, r8;"
        "mov    r1, r9;"
        "mov    r2, r10;"
        "mov    r3, r11;"
        "push   { r0-r3 };"
        "push   { r4-r7 };"
        "mov	r0, sp;"
        "bl     %c0;"
        "pop    { r4-r7 };"
        "pop    { r0-r3 };"
        "mov    r8 , r0;"
        "mov    r9 , r1;"
        "mov    r10, r2;"
        "mov    r11, r3;"
        "pop    { r0 };"
        "mov    lr, r0;"
        "bx     lr;"
#endif
        :: "i" (pendsv)
    );
    __UNREACHABLE;
}
/*
 * svc handler, used to hard switch the cpu into exception mode to return
 * to preempted thread.
 */
__NAKED void _svc(void)
{
    __asm__ volatile(
        /* load the pointer to the original exception frame we want to restore */
#if       (__CORTEX_M >= 0x03)
        "mov	sp, r4;"
        "pop	{ r4-r11, lr };"
        "bx		lr;"
#else
        "mov	sp, r4;"
        "pop    { r4-r7 };"
        "pop    { r0-r3 };"
        "mov    r8 , r0;"
        "mov    r9 , r1;"
        "mov    r10, r2;"
        "mov    r11, r3;"
        "pop	{ pc };"
#endif
    );
}

__NAKED static void _half_save_and_svc(vaddr_t *fromsp, vaddr_t tosp)
{
    __asm__ volatile(
#if       (__CORTEX_M >= 0x03)

        "push	{ r4-r11, lr };"
        "str	sp, [r0];"

        /* make sure we load the destination sp here before we reenable interrupts */
        "mov	sp, r1;"

        "clrex;"
        "cpsie 	i;"

        "mov	r4, r1;"
        "svc #0;" /* make a svc call to get us into handler mode */

#else
        "push   { lr };"
        "mov    r2, r10;"
        "mov    r3, r11;"
        "push   { r2-r3 };"
        "mov    r2, r8;"
        "mov    r3, r9;"
        "push   { r2-r3 };"
        "push   { r4-r7 };"

        "mov    r3, sp;"
        "str	r3, [r0];"
        "mov	sp, r1;"
        "cpsie 	i;"

        "mov	r4, r1;"
        "svc #0;"           /* make a svc call to get us into handler mode */
#endif
    );
}

/* simple scenario where the to and from thread yielded */
__NAKED static void _arch_non_preempt_context_switch(vaddr_t *fromsp, vaddr_t tosp)
{
    __asm__ volatile(
#if       (__CORTEX_M >= 0x03)
        "push	{ r4-r11, lr };"
        "str	sp, [r0];"

        "mov	sp, r1;"
        "pop	{ r4-r11, lr };"
        "clrex;"
        "bx		lr;"
#else
        "push   { lr };"
        "mov    r2, r10;"
        "mov    r3, r11;"
        "push   { r2-r3 };"
        "mov    r2, r8;"
        "mov    r3, r9;"
        "push   { r2-r3 };"
        "push   { r4-r7 };"

        "mov    r3, sp;"
        "str	r3, [r0];"
        "mov	sp, r1;"

        "pop    { r4-r7 };"
        "pop    { r0-r3 };"
        "mov    r8 , r0;"
        "mov    r9 , r1;"
        "mov    r10, r2;"
        "mov    r11, r3;"
        "pop    { pc };"
#endif
    );
}

__NAKED static void _thread_mode_bounce(void)
{
    __asm__ volatile(
#if       (__CORTEX_M >= 0x03)
        "pop	{ r4-r11, lr };"
        "bx		lr;"
#else
        "pop    { r4-r7 };"
        "pop    { r0-r3 };"
        "mov    r8 , r0;"
        "mov    r9 , r1;"
        "mov    r10, r2;"
        "mov    r11, r3;"
        "pop    { pc };"
#endif
    );
    __UNREACHABLE;
}

/*
 * The raw context switch routine. Called by the scheduler when it decides to switch.
 * Called either in the context of a thread yielding or blocking (interrupts disabled,
 * on the system stack), or inside the pendsv handler on a thread that is being preempted
 * (interrupts disabled, in handler mode). If preempt_frame is set the thread
 * is being preempted.
 */
void arch_context_switch(struct thread *oldthread, struct thread *newthread)
{
    LTRACE_ENTRY;

    /* if preempt_frame is set, we are being preempted */
    if (preempt_frame) {
        oldthread->arch.was_preempted = true;
        oldthread->arch.sp = (addr_t)preempt_frame;
        preempt_frame = NULL;

        LTRACEF("we're preempted, new %d\n", newthread->arch.was_preempted);
        if (newthread->arch.was_preempted) {
            /* return directly to the preempted thread's iframe */
            __asm__ volatile(
                "mov	sp, %0;"
#if       (__CORTEX_M >= 0x03)
                "cpsie	i;"
                "pop	{ r4-r11, lr };"
                "clrex;"
                "bx		lr;"
#else
                "cpsie	i;"
                "pop    { r4-r7 };"
                "pop    { r0-r3 };"
                "mov    r8 , r0;"
                "mov    r9 , r1;"
                "mov    r10, r2;"
                "mov    r11, r3;"
                "pop    { pc };"
#endif
                :: "r"(newthread->arch.sp)
            );
            __UNREACHABLE;
        } else {
            /* we're inside a pendsv, switching to a user mode thread */
            /* set up a fake frame to exception return to */
            struct arm_cm_exception_frame_short *frame = (void *)newthread->arch.sp;
            frame--;

            frame->pc = (uint32_t)&_thread_mode_bounce;
            frame->psr = (1 << 24); /* thread bit set, IPSR 0 */
            frame->r0 = frame->r1 =  frame->r2 = frame->r3 = frame->r12 = frame->lr = 99;

            LTRACEF("iretting to user space\n");
            //hexdump(frame, sizeof(*frame) + 64);

            __asm__ volatile(
#if       (__CORTEX_M >= 0x03)
                "clrex;"
#endif
                "mov	sp, %0;"
                "bx		%1;"
                :: "r"(frame), "r"(0xfffffff9)
            );
            __UNREACHABLE;
        }
    } else {
        oldthread->arch.was_preempted = false;

        if (newthread->arch.was_preempted) {
            LTRACEF("not being preempted, but switching to preempted thread\n");
            _half_save_and_svc(&oldthread->arch.sp, newthread->arch.sp);
        } else {
            /* fast path, both sides did not preempt */
            _arch_non_preempt_context_switch(&oldthread->arch.sp, newthread->arch.sp);
        }
    }

}

void arch_dump_thread(thread_t *t)
{
    if (t->state != THREAD_RUNNING) {
        dprintf(INFO, "\tarch: ");
        dprintf(INFO, "sp 0x%lx, was preempted %u\n", t->arch.sp, t->arch.was_preempted);
    }
}


