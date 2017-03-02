// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <magenta/errors.h>

mx_status_t arch_hypervisor_create(mxtl::unique_ptr<HypervisorContext>* context) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t arch_guest_create(mxtl::unique_ptr<GuestContext>* context) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t arch_guest_start(const mxtl::unique_ptr<GuestContext>& context, uintptr_t entry,
                             uintptr_t stack) {
    return ERR_NOT_SUPPORTED;
}
