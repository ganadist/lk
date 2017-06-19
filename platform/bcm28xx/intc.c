// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <assert.h>
#include <bits.h>
#include <dev/interrupt.h>
#include <err.h>
#include <kernel/mp.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <platform/bcm28xx.h>
#include <trace.h>
#include <arch/arm64.h>

#include "platform_p.h"

#define LOCAL_TRACE 0

struct int_handler_struct {
    int_handler handler;
    void* arg;
};

static struct int_handler_struct int_handler_table[MAX_INT];

static spin_lock_t lock = SPIN_LOCK_INITIAL_VALUE;

status_t mask_interrupt(unsigned int vector) {
    LTRACEF("vector %u\n", vector);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    if (vector >= INTERRUPT_ARM_LOCAL_CNTPSIRQ && vector <= INTERRUPT_ARM_LOCAL_CNTVIRQ) {
        // local timer interrupts, mask on all cpus
        for (uint cpu = 0; cpu < 4; cpu++) {
            uintptr_t reg = INTC_LOCAL_TIMER_INT_CONTROL0 + cpu * 4;

            *REG32(reg) &= (1 << (vector - INTERRUPT_ARM_LOCAL_CNTPSIRQ));
        }
    } else if (/* vector >= ARM_IRQ1_BASE && */ vector < (ARM_IRQ0_BASE + 32)) {
        uintptr_t reg;
        if (vector >= ARM_IRQ0_BASE)
            reg = INTC_DISABLE3;
        else if (vector >= ARM_IRQ2_BASE)
            reg = INTC_DISABLE2;
        else
            reg = INTC_DISABLE1;

        *REG32(reg) = 1 << (vector % 32);
    } else if ( vector >= INTERRUPT_ARM_LOCAL_MAILBOX0 && vector <= INTERRUPT_ARM_LOCAL_MAILBOX3) {
        for (uint cpu = 0; cpu < 4; cpu++) {
            uintptr_t reg = INTC_LOCAL_MAILBOX_INT_CONTROL0 + cpu * 4;
            *REG32(reg) &= ~(1 << (vector - INTERRUPT_ARM_LOCAL_MAILBOX0));
        }
    } else {
        PANIC_UNIMPLEMENTED;
    }

    spin_unlock_irqrestore(&lock, state);

    return NO_ERROR;
}

status_t unmask_interrupt(unsigned int vector) {
    LTRACEF("vector %u\n", vector);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    if (vector >= INTERRUPT_ARM_LOCAL_CNTPSIRQ && vector <= INTERRUPT_ARM_LOCAL_CNTVIRQ) {
        // local timer interrupts, unmask for all cpus
        for (uint cpu = 0; cpu < 4; cpu++) {
            uintptr_t reg = INTC_LOCAL_TIMER_INT_CONTROL0 + cpu * 4;

            *REG32(reg) |= (1 << (vector - INTERRUPT_ARM_LOCAL_CNTPSIRQ));
        }
    } else if (/* vector >= ARM_IRQ1_BASE && */ vector < (ARM_IRQ0_BASE + 32)) {
        uintptr_t reg;
        if (vector >= ARM_IRQ0_BASE)
            reg = INTC_ENABLE3;
        else if (vector >= ARM_IRQ2_BASE)
            reg = INTC_ENABLE2;
        else
            reg = INTC_ENABLE1;
        //printf("vector = %x   reg=%lx\n",vector,reg);
        //printf("basic pending = %08x\n", *(uint32_t *)0xffffffffc000b200);
        //printf("irq1  pending = %08x\n", *(uint32_t *)0xffffffffc000b204);
        //printf("irq2  pending = %08x\n", *(uint32_t *)0xffffffffc000b208);
        *REG32(reg) = 1 << (vector % 32);
    } else if ( vector >= INTERRUPT_ARM_LOCAL_MAILBOX0 && vector <= INTERRUPT_ARM_LOCAL_MAILBOX3) {
        for (uint cpu = 0; cpu < 4; cpu++) {
            uintptr_t reg = INTC_LOCAL_MAILBOX_INT_CONTROL0 + cpu * 4;
            *REG32(reg) |= 1 << (vector - INTERRUPT_ARM_LOCAL_MAILBOX0);
        }
    } else {
        PANIC_UNIMPLEMENTED;
    }

    spin_unlock_irqrestore(&lock, state);

    return NO_ERROR;
}

bool is_valid_interrupt(unsigned int vector, uint32_t flags) {
    return (vector < MAX_INT);
}

unsigned int remap_interrupt(unsigned int vector) {
    return vector;
}

/*
 *  TODO(hollande) - Implement!
 */
status_t configure_interrupt(unsigned int vector,
                             enum interrupt_trigger_mode tm,
                             enum interrupt_polarity pol)
{
    return NO_ERROR;
}

/*
 *  TODO(hollande) - Implement!
 */
status_t get_interrupt_config(unsigned int vector,
                              enum interrupt_trigger_mode* tm,
                              enum interrupt_polarity* pol)
{
    if (tm)  *tm  = IRQ_TRIGGER_MODE_EDGE;
    if (pol) *pol = IRQ_POLARITY_ACTIVE_HIGH;
    return NO_ERROR;
}

void register_int_handler(unsigned int vector, int_handler handler, void* arg) {
    if (vector >= MAX_INT)
        panic("register_int_handler: vector out of range %u\n", vector);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&lock, state);

    int_handler_table[vector].handler = handler;
    int_handler_table[vector].arg = arg;

    spin_unlock_irqrestore(&lock, state);
}

enum handler_return platform_irq(struct arm64_iframe_short* frame) {
    uint vector;
    uint cpu = arch_curr_cpu_num();

    THREAD_STATS_INC(interrupts);

    // see what kind of irq it is
    uint32_t pend = *REG32(INTC_LOCAL_IRQ_PEND0 + cpu * 4);

    pend &= ~(1 << (INTERRUPT_ARM_LOCAL_GPU_FAST % 32)); // mask out gpu interrupts

    if (pend != 0) {
        // it's a local interrupt
        LTRACEF("local pend 0x%x\n", pend);
        vector = ARM_IRQ_LOCAL_BASE + ctz(pend);
        goto decoded;
    }

// XXX disable for now, since all of the interesting irqs are mirrored into the other banks
#if 0
    // look in bank 0 (ARM interrupts)
    pend = *REG32(INTC_PEND0);
    LTRACEF("pend0 0x%x\n", pend);
    pend &= ~((1<<8)|(1<<9)); // mask out bit 8 and 9
    if (pend != 0) {
        // it's a bank 0 interrupt
        vector = ARM_IRQ0_BASE + ctz(pend);
        goto decoded;
    }
#endif

    // look for VC interrupt bank 1
    pend = *REG32(INTC_PEND1);
    LTRACEF("pend1 0x%x\n", pend);
    if (pend != 0) {
        // it's a bank 1 interrupt
        vector = ARM_IRQ1_BASE + ctz(pend);
        goto decoded;
    }

    // look for VC interrupt bank 2
    pend = *REG32(INTC_PEND2);
    LTRACEF("pend2 0x%x\n", pend);
    if (pend != 0) {
        // it's a bank 2 interrupt
        vector = ARM_IRQ2_BASE + ctz(pend);
        goto decoded;
    }

    vector = 0xffffffff;

decoded:
    LTRACEF("cpu %u vector %u\n", cpu, vector);

    // dispatch the irq
    enum handler_return ret = INT_NO_RESCHEDULE;

#if WITH_SMP
    if (vector == INTERRUPT_ARM_LOCAL_MAILBOX0) {
        pend = *REG32(INTC_LOCAL_MAILBOX0_CLR0 + 0x10 * cpu);
        LTRACEF("mailbox0 clr 0x%x\n", pend);

        // ack it
        *REG32(INTC_LOCAL_MAILBOX0_CLR0 + 0x10 * cpu) = pend;

        if (pend & (1 << MP_IPI_GENERIC)) {
            ret = mp_mbx_generic_irq();
        }
        if (pend & (1 << MP_IPI_RESCHEDULE)) {
            ret = mp_mbx_reschedule_irq();
        }
    } else
#endif // WITH_SMP
        if (vector == 0xffffffff) {
        ret = INT_NO_RESCHEDULE;
    } else if (int_handler_table[vector].handler) {
        ret = int_handler_table[vector].handler(int_handler_table[vector].arg);
    } else {
        panic("irq %u fired on cpu %u but no handler set!\n", vector, cpu);
    }

    return ret;
}

enum handler_return platform_fiq(struct arm64_iframe_short* frame) {
    PANIC_UNIMPLEMENTED;
}

/* called from arm64 code. TODO: put in shared header */
void bcm28xx_send_ipi(uint irq, uint cpu_mask);

void bcm28xx_send_ipi(uint irq, uint cpu_mask) {
    LTRACEF("irq %u, cpu_mask 0x%x\n", irq, cpu_mask);

    for (uint i = 0; i < 4; i++) {
        if (cpu_mask & (1 << i)) {
            LTRACEF("sending to cpu %u\n", i);
            *REG32(INTC_LOCAL_MAILBOX0_SET0 + 0x10 * i) = (1 << irq);
        }
    }
}

void intc_init(void) {
    // mask everything
    *REG32(INTC_DISABLE1) = 0xffffffff;
    *REG32(INTC_DISABLE2) = 0xffffffff;
    *REG32(INTC_DISABLE3) = 0xffffffff;

#if WITH_SMP
    // unable mailbox irqs on all cores
    for (uint i = 0; i < 4; i++) {
        *REG32(INTC_LOCAL_MAILBOX_INT_CONTROL0 + 0x4 * i) = 0x1;
    }
#endif
}
