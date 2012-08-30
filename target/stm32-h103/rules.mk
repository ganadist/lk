LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

STM32_CHIP := stm32f103_md

PLATFORM := stm32f1xx

DEFINES += \
	ENABLE_UART1=1 \
	TARGET_HAS_DEBUG_LED=1

INCLUDES += -I$(LOCAL_DIR)/include

MODULE_SRCS += \
	$(LOCAL_DIR)/init.c

include make/module.mk

