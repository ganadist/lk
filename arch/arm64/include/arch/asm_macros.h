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
#pragma once

.macro push ra, rb
stp \ra, \rb, [sp,#-16]!
.endm

.macro pop ra, rb
ldp \ra, \rb, [sp], #16
.endm

.macro tbzmask, reg, mask, label, shift=0
.if \shift >= 64
    .error "tbzmask: unsupported mask, \mask"
.elseif \mask == 1 << \shift
    tbz     \reg, #\shift, \label
.else
    tbzmask \reg, \mask, \label, "(\shift + 1)"
.endif
.endm

.macro tbnzmask, reg, mask, label, shift=0
.if \shift >= 64
    .error "tbnzmask: unsupported mask, \mask"
.elseif \mask == 1 << \shift
    tbnz     \reg, #\shift, \label
.else
    tbnzmask \reg, \mask, \label, "(\shift + 1)"
.endif
.endm
