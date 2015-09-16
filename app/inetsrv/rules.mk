LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/inetsrv.c \
	$(LOCAL_DIR)/tftp.c \

MODULE_DEPS := \
    lib/cksum \
    lib/minip \

include make/module.mk
