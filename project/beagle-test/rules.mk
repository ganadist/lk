# top level project rules for the armemu-test project
#
LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := beagle

MODULES += \
	app/tests \
	app/stringtests \
	lib/console

OBJS += \
	$(LOCAL_DIR)/init.o

