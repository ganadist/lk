LOCAL_DIR := $(GET_LOCAL_DIR)

ARCH := arm
ARM_CPU := cortex-a8
CPU := generic

INCLUDES += \
	-I$(LOCAL_DIR)/include

OBJS += \
	$(LOCAL_DIR)/debug.o \
	$(LOCAL_DIR)/interrupts.o \
	$(LOCAL_DIR)/platform.o \
	$(LOCAL_DIR)/timer.o \
	$(LOCAL_DIR)/uart.o

MEMBASE := 0x80000000

DEFINES += MEMBASE=$(MEMBASE)

LINKER_SCRIPT += \
	$(BUILDDIR)/system-onesegment.ld

