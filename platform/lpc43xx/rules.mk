LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ROMBASE ?= 0x00000000
MEMBASE ?= 0x10080000
MEMSIZE ?= 40960

ARCH := arm
ARM_CPU := cortex-m4

GLOBAL_DEFINES += \
	MEMSIZE=$(MEMSIZE)

GLOBAL_INCLUDES += \
	$(LOCAL_DIR)/include

MODULE_SRCS += \
	$(LOCAL_DIR)/init.c \
	$(LOCAL_DIR)/vectab.c \
	$(LOCAL_DIR)/debug.c

LINKER_SCRIPT += \
	$(BUILDDIR)/system-twosegment.ld

MODULE_DEPS += \
	arch/arm/arm-m/systick

include make/module.mk
