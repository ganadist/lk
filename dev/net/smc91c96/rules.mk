LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

INCLUDES += \
	-I$(LOCAL_DIR)/include

MODULE_SRCS += \
	$(LOCAL_DIR)/smc91c96.c

include make/module.mk
