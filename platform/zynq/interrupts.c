/*
 * Copyright (c) 2014 Travis Geiselbrecht
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
#include <err.h>
#include <sys/types.h>
#include <debug.h>
#include <trace.h>
#include <reg.h>
#include <kernel/thread.h>
#include <kernel/debug.h>
#include <platform/interrupts.h>
#include <arch/ops.h>
#include <arch/arm.h>
#include <platform/zynq.h>
#include "platform_p.h"

/* driver for GIC */

#define LOCAL_TRACE 0

struct int_handler_struct {
    int_handler handler;
    void *arg;
};

static struct int_handler_struct int_handler_table[MAX_INT];

void register_int_handler(unsigned int vector, int_handler handler, void *arg)
{
    if (vector >= MAX_INT)
        panic("register_int_handler: vector out of range %d\n", vector);

    enter_critical_section();

    int_handler_table[vector].handler = handler;
    int_handler_table[vector].arg = arg;

    exit_critical_section();
}

#define GICCPUREG(reg) (*REG32(GIC_PROC_BASE + (reg)))
#define GICDISTREG(reg) (*REG32(GIC_DISTRIB_BASE + (reg)))

/* main cpu regs */
#define CONTROL  (0x00)
#define PMR      (0x04)
#define BR       (0x08)
#define IAR      (0x0c)
#define EOIR     (0x10)
#define RPR      (0x14)
#define HPPIR    (0x18)
#define ABPR     (0x1c)
#define AIAR     (0x20)
#define AEOIR    (0x24)
#define AHPPIR   (0x28)

/* distribution regs */
#define DISTCONTROL (0x000)
#define GROUP       (0x080)
#define SETENABLE   (0x100)
#define CLRENABLE   (0x180)
#define SETPEND     (0x200)
#define CLRPEND     (0x280)
#define SETACTIVE   (0x300)
#define CLRACTIVE   (0x380)
#define PRIORITY    (0x400)
#define _TARGET     (0x800)
#define CONFIG      (0xc00)
#define NSACR       (0xe00)
#define SGIR        (0xf00)

static void gic_set_enable(uint vector, bool enable)
{
    if (enable) {
        uint regoff = SETENABLE + 4 * (vector / 32);
        GICDISTREG(regoff) = (1 << (vector % 32));
    } else {
        uint regoff = CLRENABLE + 4 * (vector / 32);
        GICDISTREG(regoff) = (1 << (vector % 32));
    }
}

void platform_init_interrupts(void)
{
    GICDISTREG(DISTCONTROL) = 0;

    GICDISTREG(CLRENABLE) = 0xffff0000;
    GICDISTREG(SETENABLE) = 0x0000ffff;
    GICDISTREG(CLRPEND) = 0xffffffff;
    GICDISTREG(GROUP) = 0;
    GICCPUREG(PMR) = 0xf0;

    for (int i = 0; i < 32 / 4; i++) {
        GICDISTREG(PRIORITY + i * 4) = 0x80808080;
    }

    for (int i = 32/16; i < MAX_INT / 16; i++) {
        GICDISTREG(NSACR + i * 4) = 0xffffffff;
    }
    for (int i = 32/32; i < MAX_INT / 32; i++) {
        GICDISTREG(CLRENABLE + i * 4) = 0xffffffff;
        GICDISTREG(CLRPEND + i * 4) = 0xffffffff;
        GICDISTREG(GROUP + i * 4) = 0;
    }

    for (int i = 32/4; i < MAX_INT / 4; i++) {
        GICDISTREG(_TARGET + i * 4) = 0x01010101; // assign all irqs to cpu 0
        GICDISTREG(PRIORITY + i * 4) = 0x80808080;
    }

    GICDISTREG(DISTCONTROL) = 1; // enable GIC0, IRQ only
    GICCPUREG(CONTROL) = (0<<3)|(0<<2)||1; // enable GIC0, IRQ only, group 0 set to IRQ
}

status_t mask_interrupt(unsigned int vector)
{
    if (vector >= MAX_INT)
        return -1;

    enter_critical_section();

    gic_set_enable(vector, false);

    exit_critical_section();

    return NO_ERROR;
}

status_t unmask_interrupt(unsigned int vector)
{
    if (vector >= MAX_INT)
        return -1;

    enter_critical_section();

    gic_set_enable(vector, true);

    exit_critical_section();

    return NO_ERROR;
}

enum handler_return platform_irq(struct arm_iframe *frame)
{
    uint32_t iar = GICCPUREG(IAR);
    uint vector = iar & 0x3ff;

    if (vector >= 0x3fe) {
        // spurious
        return INT_NO_RESCHEDULE;
    }

    inc_critical_section();

    LTRACEF("platform_irq: spsr 0x%x, pc 0x%x, currthread %p, vector %d\n", frame->spsr, frame->pc, current_thread, vector);

    THREAD_STATS_INC(interrupts);
    KEVLOG_IRQ_ENTER(vector);

    // deliver the interrupt
    enum handler_return ret;

    ret = INT_NO_RESCHEDULE;
    if (int_handler_table[vector].handler)
        ret = int_handler_table[vector].handler(int_handler_table[vector].arg);

    GICCPUREG(EOIR) = iar;

    LTRACEF("platform_irq: exit %d\n", ret);

    KEVLOG_IRQ_EXIT(vector);

    if (ret != INT_NO_RESCHEDULE)
        thread_preempt();

    dec_critical_section();

    return ret;
}

void platform_fiq(struct arm_iframe *frame)
{
    PANIC_UNIMPLEMENTED;

    uint32_t iar = GICCPUREG(IAR);
    uint vector = iar & 0x3ff;

    //printf("fiq %d\n", vector);

    if (vector >= 0x3fe) {
        // spurious
        return;
    }

    //shutdown();

    GICCPUREG(EOIR) = iar;
}

/* vim: set ts=4 sw=4 expandtab: */
