// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_address_region.h>

#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/user_copy.h>
#include <magenta/vm_address_region_dispatcher.h>
#include <magenta/vm_object_dispatcher.h>

#include <mxtl/auto_call.h>
#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_vmar_allocate(mx_handle_t parent_vmar_handle,
                    size_t offset, size_t size, uint32_t flags,
                    mx_handle_t* _child_vmar, uintptr_t* _child_addr) {

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_rights_t vmar_rights;
    mx_status_t status = up->GetDispatcher(parent_vmar_handle, &vmar, &vmar_rights);
    if (status != NO_ERROR)
        return status;

    // test to see if the requested mapping protections are allowed
    if ((flags & MX_VM_FLAG_CAN_MAP_READ) && !(vmar_rights & MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;
    if ((flags & MX_VM_FLAG_CAN_MAP_WRITE) && !(vmar_rights & MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;
    if ((flags & MX_VM_FLAG_CAN_MAP_EXECUTE) && !(vmar_rights & MX_RIGHT_EXECUTE))
        return ERR_ACCESS_DENIED;

    // Create the new VMAR
    mxtl::RefPtr<VmAddressRegion> new_vmar;
    status = vmar->Allocate(offset, size, flags, &new_vmar);
    if (status != NO_ERROR)
        return status;

    // Setup a handler to destroy the new VMAR if the syscall is unsuccessful.
    // Note that new_vmar is being passed by value, so a new reference is held
    // there.
    auto cleanup_handler = mxtl::MakeAutoCall([new_vmar]() {
        new_vmar->Destroy();
    });

    if (make_user_ptr(_child_addr).copy_to_user(new_vmar->base()) != NO_ERROR)
        return ERR_INVALID_ARGS;

    // Create a dispatcher
    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t new_rights;
    status = VmAddressRegionDispatcher::Create(mxtl::move(new_vmar), &dispatcher, &new_rights);
    if (status != NO_ERROR)
        return status;

    // Create a handle and attach the dispatcher to it
    HandleUniquePtr handle(MakeHandle(mxtl::move(dispatcher), new_rights));
    if (!handle)
        return ERR_NO_MEMORY;

    if (make_user_ptr(_child_vmar).copy_to_user(up->MapHandleToValue(handle.get())) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    cleanup_handler.cancel();
    return NO_ERROR;
}

mx_status_t sys_vmar_destroy(mx_handle_t vmar_handle) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_rights_t vmar_rights;
    mx_status_t status = up->GetDispatcher(vmar_handle, &vmar, &vmar_rights);
    if (status != NO_ERROR)
        return status;

    return vmar->Destroy();
}

mx_status_t sys_vmar_map(mx_handle_t vmar_handle, size_t vmar_offset,
                    mx_handle_t vmo_handle, uint64_t vmo_offset, size_t len, uint32_t flags,
                    uintptr_t* _mapped_addr) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the VMAR dispatcher from handle
    mxtl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_rights_t vmar_rights;
    mx_status_t status = up->GetDispatcher(vmar_handle, &vmar, &vmar_rights);
    if (status != NO_ERROR)
        return status;

    // lookup the VMO dispatcher from handle
    mxtl::RefPtr<VmObjectDispatcher> vmo;
    mx_rights_t vmo_rights;
    status = up->GetDispatcher(vmo_handle, &vmo, &vmo_rights);
    if (status != NO_ERROR)
        return status;

    // test to see if we should even be able to map this
    if (!(vmo_rights & MX_RIGHT_MAP))
        return ERR_ACCESS_DENIED;

    if (!VmAddressRegionDispatcher::is_valid_mapping_protection(flags))
        return ERR_INVALID_ARGS;


    // Usermode is not allowed to specify these flags on mappings, though we may
    // set them below.
    if (flags & (MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE | MX_VM_FLAG_CAN_MAP_EXECUTE)) {
        return ERR_INVALID_ARGS;
    }

    // Permissions allowed by both the VMO and the VMAR
    const bool can_read = (vmo_rights & MX_RIGHT_READ) && (vmar_rights & MX_RIGHT_READ);
    const bool can_write = (vmo_rights & MX_RIGHT_WRITE) && (vmar_rights & MX_RIGHT_WRITE);
    const bool can_exec = (vmo_rights & MX_RIGHT_EXECUTE) && (vmar_rights & MX_RIGHT_EXECUTE);

    // test to see if the requested mapping protections are allowed
    if ((flags & MX_VM_FLAG_PERM_READ) && !can_read)
        return ERR_ACCESS_DENIED;
    if ((flags & MX_VM_FLAG_PERM_WRITE) && !can_write)
        return ERR_ACCESS_DENIED;
    if ((flags & MX_VM_FLAG_PERM_EXECUTE) && !can_exec)
        return ERR_ACCESS_DENIED;

    // If a permission is allowed by both the VMO and the VMAR, add it to the
    // flags for the new mapping, so that the VMO's rights as of now can be used
    // to constrain future permission changes via Protect().
    if (can_read)
        flags |= MX_VM_FLAG_CAN_MAP_READ;
    if (can_write)
        flags |= MX_VM_FLAG_CAN_MAP_WRITE;
    if (can_exec)
        flags |= MX_VM_FLAG_CAN_MAP_EXECUTE;

    mxtl::RefPtr<VmMapping> vm_mapping;
    status = vmar->Map(vmar_offset, vmo->vmo(), vmo_offset, len, flags, &vm_mapping);
    if (status != NO_ERROR)
        return status;

    // Setup a handler to destroy the new mapping if the syscall is unsuccessful.
    auto cleanup_handler = mxtl::MakeAutoCall([vm_mapping]() {
        vm_mapping->Destroy();
    });

    if (make_user_ptr(_mapped_addr).copy_to_user(vm_mapping->base()) != NO_ERROR)
        return ERR_INVALID_ARGS;

    cleanup_handler.cancel();
    return NO_ERROR;
}

mx_status_t sys_vmar_unmap(mx_handle_t vmar_handle, uintptr_t addr, size_t len) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_rights_t vmar_rights;
    mx_status_t status = up->GetDispatcher(vmar_handle, &vmar, &vmar_rights);
    if (status != NO_ERROR)
        return status;

    if (addr < vmar->vmar()->base())
        return ERR_INVALID_ARGS;

    size_t offset = addr - vmar->vmar()->base();
    return vmar->Unmap(offset, len);
}

mx_status_t sys_vmar_protect(mx_handle_t vmar_handle, uintptr_t addr, size_t len, uint32_t prot) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    mxtl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_rights_t vmar_rights;
    mx_status_t status = up->GetDispatcher(vmar_handle, &vmar, &vmar_rights);
    if (status != NO_ERROR)
        return status;

    if (!VmAddressRegionDispatcher::is_valid_mapping_protection(prot))
        return ERR_INVALID_ARGS;

    if ((prot & MX_VM_FLAG_PERM_READ) && !(vmar_rights & MX_RIGHT_READ))
        return ERR_ACCESS_DENIED;
    if ((prot & MX_VM_FLAG_PERM_WRITE) && !(vmar_rights & MX_RIGHT_WRITE))
        return ERR_ACCESS_DENIED;
    if ((prot & MX_VM_FLAG_PERM_EXECUTE) && !(vmar_rights & MX_RIGHT_EXECUTE))
        return ERR_ACCESS_DENIED;

    if (addr < vmar->vmar()->base())
        return ERR_INVALID_ARGS;

    size_t offset = addr - vmar->vmar()->base();
    return vmar->Protect(offset, len, prot);
}
