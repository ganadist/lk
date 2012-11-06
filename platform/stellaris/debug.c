/*
 * Copyright (c) 2012 Ian McKellar
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
#include <stdarg.h>
#include <reg.h>
#include <debug.h>
#include <printf.h>
#include <lib/cbuf.h>
#include <kernel/thread.h>
#include <platform/debug.h>
#include <arch/ops.h>
#include <target/debugconfig.h>
#include <arch/arm/cm3.h>

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/debug.h"
#include "driverlib/fpu.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/uart.h"
#include "driverlib/sysctl.h"

#define DEBUG_UART UART0_BASE


static cbuf_t debug_rx_buf;

void stellaris_uart_irq(void)
{
	inc_critical_section();


    //
    // Get the interrrupt status.
    //
    unsigned long ulStatus = UARTIntStatus(DEBUG_UART, true);

    //
    // Clear the asserted interrupts.
    //
    UARTIntClear(DEBUG_UART, ulStatus);

    //
    // Loop while there are characters in the receive FIFO.
    //
    while(UARTCharsAvail(DEBUG_UART))
    {
        //
        // Read the next character from the UART and write it back to the UART.
        //
        unsigned char c = UARTCharGetNonBlocking(DEBUG_UART);
		    cbuf_write(&debug_rx_buf, &c, 1, false);


        cm3_trigger_preempt();
    }

	dec_critical_section();
}

void stellaris_debug_early_init(void)
{

  SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
  SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);

  
    // Set GPIO A0 and A1 as UART pins.
    //
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);



  UARTConfigSetExpClk(DEBUG_UART, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8|UART_CONFIG_STOP_ONE|UART_CONFIG_PAR_NONE);

  UARTEnable(DEBUG_UART);

}

void stellaris_debug_init(void)
{
    //
    // Enable the UART interrupt.
    //
    //ROM_IntEnable(INT_UART0);
    UARTIntEnable(DEBUG_UART, UART_INT_RX | UART_INT_RT);

}

void platform_dputc(char c)
{
	if (c == '\n') {
		_dputc('\r');
	}

  UARTCharPut(DEBUG_UART, c);
}

int platform_dgetc(char *c, bool wait)
{
	return cbuf_read(&debug_rx_buf, c, 1, wait);
}

void platform_halt(void)
{
	dprintf(ALWAYS, "HALT: spinning forever...\n");
	for(;;);
}


// TI driverlib debug function
extern void __error__(char *pcFilename, unsigned long ulLine)
{
}
