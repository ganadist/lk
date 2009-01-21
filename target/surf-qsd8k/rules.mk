LOCAL_DIR := $(GET_LOCAL_DIR)

INCLUDES += -I$(LOCAL_DIR)/include

PLATFORM := qsd8k

MEMBASE := 0x00000000 # SMI
MEMSIZE := 0x00800000 # 8MB

KEYS_USE_GPIO_KEYPAD := 1

MODULES += dev/keys

DEFINES += SDRAM_SIZE=$(MEMSIZE)

OBJS += \
	$(LOCAL_DIR)/init.o \
	$(LOCAL_DIR)/keypad.o


