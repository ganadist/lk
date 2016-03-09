LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

STM32_CHIP := stm32f756

PLATFORM := stm32f7xx

SDRAM_SIZE := 0x00800000
SDRAM_BASE := 0xc0000000

GLOBAL_DEFINES += \
    ENABLE_UART3=1 \
    ENABLE_SDRAM=1 \
    USE_HSE_XTAL=1 \
    SDRAM_BASE=$(SDRAM_BASE) \
    SDRAM_SIZE=$(SDRAM_SIZE) \
    PLL_M_VALUE=8 \
    PLL_N_VALUE=336 \
    PLL_P_VALUE=2 \
\
    PKTBUF_POOL_SIZE=16 \
\
    TARGET_HAS_DEBUG_LED=1


GLOBAL_INCLUDES += $(LOCAL_DIR)/include

MODULE_SRCS += \
    $(LOCAL_DIR)/init.c \
    $(LOCAL_DIR)/usb.c \

MODULE_DEPS += \
    dev/usb

include make/module.mk

