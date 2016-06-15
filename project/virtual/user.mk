# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# modules needed to implement user space

GLOBAL_DEFINES += WITH_DEBUG_LINEBUFFER=1

MODULES += \
    ulib/musl \
    lib/syscalls \
    lib/userboot \
    lib/debuglog \
    uapp/crasher \
    uapp/devmgr \
    uapp/i2c \
    uapp/mxsh \
    uapp/strerror \
    uapp/userboot \
    uapp/dlog \
    uapp/netsvc \
    ulib/magenta \
    ulib/mojo \
    ulib/mxio \
    ulib/inet6 \
    utest \

# if we're not embedding the bootfs, build a standalone image
ifneq ($(EMBED_USER_BOOTFS),true)
EXTRA_BUILDDEPS += $(USER_FS)
endif

EXTRA_BUILDDEPS += $(USER_BOOTFS)

MKBOOTFS := $(BUILDDIR)/tools/mkbootfs

$(MKBOOTFS): tools/mkbootfs.c
	@echo compiling $@
	@$(MKDIR)
	cc -Wall -o $@ $<

$(USER_BOOTFS): $(MKBOOTFS) $(USER_MANIFEST)
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)$(MKBOOTFS) -o $(USER_BOOTFS) $(USER_MANIFEST)

GENERATED += $(USER_BOOTFS) $(MKBOOTFS) $(USERBOOT_BIN)
