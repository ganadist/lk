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
#include <err.h>
#include <debug.h>
#include <target.h>
#include <compiler.h>

#include "ti_driverlib.h"

void target_early_init(void)
{
	SysCtlPeripheralPowerOn(SYSCTL_PERIPH_GPIOF);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);

	GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, 0);
	GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 0);
	GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_3, 0);

	GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
	GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_2, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
	GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_3, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
	GPIODirModeSet(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_DIR_MODE_OUT);
	GPIODirModeSet(GPIO_PORTF_BASE, GPIO_PIN_2, GPIO_DIR_MODE_OUT);
	GPIODirModeSet(GPIO_PORTF_BASE, GPIO_PIN_3, GPIO_DIR_MODE_OUT);
}

void target_init(void)
{
}

void target_set_debug_led(unsigned int led, bool on)
{
	switch (led) {
		case 0: GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, on ? GPIO_PIN_1 : 0); break;
		case 1: GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, on ? GPIO_PIN_2 : 0); break;
		case 2: GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_3, on ? GPIO_PIN_3 : 0); break;
	}
}

