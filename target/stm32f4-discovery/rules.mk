LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

STM32_CHIP := stm32f407

PLATFORM := stm32f4xx

GLOBAL_DEFINES += \
	ENABLE_UART2=1 \
	TARGET_HAS_DEBUG_LED=1 \
	HSE_VALUE=8000000

GLOBAL_INCLUDES += $(LOCAL_DIR)/include

MODULE_SRCS += \
	$(LOCAL_DIR)/init.c

include make/module.mk

