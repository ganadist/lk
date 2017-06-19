LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ifeq ($(ARCH),)
ARCH := arm64
endif
ifeq ($(ARCH),arm64)
ARM_CPU ?= cortex-a53
endif
ifeq ($(ARCH),arm)
ARM_CPU ?= cortex-a15
endif
WITH_SMP ?= 1

GLOBAL_INCLUDES += \
    $(LOCAL_DIR)/include

MODULE_SRCS += \
    $(LOCAL_DIR)/debug.c \
    $(LOCAL_DIR)/platform.c \
    $(LOCAL_DIR)/uart.c

MEMBASE := 0x40000000
MEMSIZE := 0x08000000   # 512MB

MODULE_DEPS += \
    lib/cbuf \
    dev/interrupt/arm_gic \
    dev/timer/arm_generic \
    dev/virtio/block \
    dev/virtio/net \

GLOBAL_DEFINES += \
    MEMBASE=$(MEMBASE) \
    MEMSIZE=$(MEMSIZE) \
    MMU_WITH_TRAMPOLINE=1 # use the trampoline translation table in start.S

LINKER_SCRIPT += \
    $(BUILDDIR)/system-onesegment.ld

include make/module.mk
