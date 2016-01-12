/*
 * Copyright (c) 2012-2015 Travis Geiselbrecht
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
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

static void initial_thread_func(void) __NO_RETURN;
static void initial_thread_func(void)
{
    int ret;

    LTRACEF("thread %p calling %p with arg %p\n", _current_thread, _current_thread->entry, _current_thread->arg);
#if LOCAL_TRACE
    dump_thread(_current_thread);
#endif

    /* release the thread lock that was implicitly held across the reschedule */
    spin_unlock(&thread_lock);
    arch_enable_ints();

    ret = _current_thread->entry(_current_thread->arg);

    LTRACEF("thread %p exiting with %d\n", _current_thread, ret);

    thread_exit(ret);
}

void arch_thread_initialize(struct thread *t)
{
    LTRACEF("thread %p, stack %p\n", t, t->stack);

    /* find the top of the stack and align it on an 8 byte boundary */
    uint32_t *sp = (void *)ROUNDDOWN((vaddr_t)t->stack + t->stack_size, 8);

    struct arm_cm_context_switch_frame *frame = (void *)sp;
    frame--;

    /* arrange for lr to point to our starting routine */
    frame->lr = (uint32_t)&initial_thread_func;

    t->arch.sp = (addr_t)frame;
    t->arch.was_preempted = false;

#if __FPU_PRESENT
    /* zero the fpu register state */
    memset(t->arch.fpregs, 0, sizeof(t->arch.fpregs));
    t->arch.fpused = false;
#endif
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

__NAKED static void _half_save_and_svc(struct thread *oldthread, struct thread *newthread, bool fpu_save, bool restore_fpu)
{
    __asm__ volatile(
#if       (__CORTEX_M >= 0x03)

        "push	{ r4-r11, lr };"
        "str	sp, [r0];"

        /* see if we need to restore fpu context */
        "tst    r3, #1;"
        "beq    0f;"

        /* restore the top part of the fpu context */
        "add    r3, r1, %[fp_off];"
        "vldm   r3, { s16-s31 };"

        /* restore the bottom part of the context, stored up the frame a little bit */
        "add    r3, sp, %[fp_exc_off];"
        "vldm   r3!, { s0-s15 };"
        "ldr    r3, [r3];"
        "vmsr   fpscr, r3;"

        "clrex;"
        "cpsie  i;"

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
__NAKED static void _arch_non_preempt_context_switch(struct thread *oldthread, struct thread *newthread, bool save_fpu, bool restore_fpu)
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
    //LTRACE_ENTRY;
    LTRACEF("FPCCR.LSPACT %lu, FPCAR 0x%x, CONTROL.FPCA %lu\n",
            FPU->FPCCR & FPU_FPCCR_LSPACT_Msk, FPU->FPCAR, __get_CONTROL() & CONTROL_FPCA_Msk);

    asm volatile("vmov s0, s0");

    /* if preempt_frame is set, we are being preempted */
    if (preempt_frame) {
        LTRACEF("we're preempted, old frame %p, old lr 0x%x, pc 0x%x, new preempted bool %d\n",
                preempt_frame, preempt_frame->lr, preempt_frame->pc, newthread->arch.was_preempted);

#if __FPU_PRESENT
        /* see if extended fpu frame was pushed */
        if ((preempt_frame->lr & (1<<4)) == 0) {
            /* force a context save if it hasn't already */
            asm volatile("vmov s0, s0" ::: "memory");
            asm volatile("isb");

            /* save the top part of the context */
            asm volatile("vstm %0, { s16-s31 }" :: "r" (&oldthread->arch.fpregs[0]));
            oldthread->arch.fpused = true;

            /* verify that FPCCR.LSPACT was cleared and CONTROL.FPCA was set */
            DEBUG_ASSERT((FPU->FPCCR & FPU_FPCCR_LSPACT_Msk) == 0);
            DEBUG_ASSERT(__get_CONTROL() & CONTROL_FPCA_Msk);
        } else {
            DEBUG_ASSERT(oldthread->arch.fpused == false);
        }
#endif
        oldthread->arch.was_preempted = true;
        oldthread->arch.sp = (addr_t)preempt_frame;
        preempt_frame = NULL;

#if __FPU_PRESENT
        if (newthread->arch.fpused) {
            /* restore the old fpu state */
            DEBUG_ASSERT((FPU->FPCCR & FPU_FPCCR_LSPACT_Msk) == 0);
            DEBUG_ASSERT(__get_CONTROL() & CONTROL_FPCA_Msk);

            LTRACEF("newthread FPCCR.LSPACT %lu, FPCAR 0x%x, CONTROL.FPCA %lu\n",
                    FPU->FPCCR & FPU_FPCCR_LSPACT_Msk, FPU->FPCAR, __get_CONTROL() & CONTROL_FPCA_Msk);

            asm volatile("vldm %0, { s16-s31 }" :: "r" (&newthread->arch.fpregs[0]));
        }
#endif
        if (newthread->arch.was_preempted) {
            /* return directly to the preempted thread's iframe */
            LTRACEF("newthread2 FPCCR.LSPACT %lu, FPCAR 0x%x, CONTROL.FPCA %lu\n",
                    FPU->FPCCR & FPU_FPCCR_LSPACT_Msk, FPU->FPCAR, __get_CONTROL() & CONTROL_FPCA_Msk);
            //hexdump((void *)newthread->arch.sp, 128+64);
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
            frame->r0 = newthread->arch.fpused;
            frame->r1 = frame->r2 = frame->r3 = frame->r12 = frame->lr = 99;

            LTRACEF("iretting to user space, fpused %u\n", newthread->arch.fpused);
            //hexdump(frame, 128+32);

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

#if __FPU_PRESENT
        /* see if we have fpu state we need to save */
        if (oldthread->arch.fpused || __get_CONTROL() & CONTROL_FPCA_Msk) {
            /* mark this thread as using float */
            oldthread->arch.fpused = true;
        }
#endif

        if (newthread->arch.was_preempted) {
            LTRACEF("not being preempted, but switching to preempted thread\n");
            //hexdump((void *)newthread->arch.sp, 128+32);
            _half_save_and_svc(oldthread, newthread, oldthread->arch.fpused, newthread->arch.fpused);
        } else {
            /* fast path, both sides did not preempt */
            LTRACEF("both sides are not preempted newsp 0x%lx %u %u\n", newthread->arch.sp, oldthread->arch.fpused, newthread->arch.fpused);
            //hexdump((void *)newthread->arch.sp, 128);
            _arch_non_preempt_context_switch(oldthread, newthread, oldthread->arch.fpused, newthread->arch.fpused);
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


