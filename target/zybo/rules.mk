LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

PLATFORM := zynq

# set the system base to sram
ZYNQ_USE_SRAM ?= 1

# we have sdram
ZYNQ_SDRAM_SIZE := 0x10000000

GLOBAL_INCLUDES += \
	$(LOCAL_DIR)/include

GLOBAL_DEFINES += \
	EXTERNAL_CLOCK_FREQ=50000000

MODULE_SRCS += \
	$(LOCAL_DIR)/target.c

include make/module.mk
