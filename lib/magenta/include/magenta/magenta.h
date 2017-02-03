// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/handle.h>
#include <magenta/types.h>

#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

class Dispatcher;
class ExceptionPort;
class ProcessDispatcher;
class JobDispatcher;

// Creates a handle attached to |dispatcher| and with |rights| from a
// specific arena which makes their addresses come from a fixed range.
Handle* MakeHandle(mxtl::RefPtr<Dispatcher> dispatcher, mx_rights_t rights);

// Duplicate a handle created by MakeHandle().
Handle* DupHandle(Handle* source, mx_rights_t rights);

// Maps a handle created by MakeHandle() to the 0 to 2^32 range.
uint32_t MapHandleToU32(const Handle* handle);

// Maps an integer obtained by MapHandleToU32() back to a Handle.
Handle* MapU32ToHandle(uint32_t value);

// Set/get the system exception port.
mx_status_t SetSystemExceptionPort(mxtl::RefPtr<ExceptionPort> eport);
void ResetSystemExceptionPort();
mxtl::RefPtr<ExceptionPort> GetSystemExceptionPort();

mxtl::RefPtr<JobDispatcher> GetRootJobDispatcher();

bool magenta_rights_check(const Handle* handle, mx_rights_t desired);

// (temporary) conversion from mx_time (nanoseconds) to lk_time_t (milliseconds)
// remove once mx_time_t is converted to 1:1 match mx_time_t
inline lk_time_t mx_time_to_lk(mx_time_t mxt) {
    if (mxt == MX_TIME_INFINITE)
        return INFINITE_TIME;

    uint64_t temp = mxt / 1000000ull; // nanosecs to milliseconds
    if (temp >= UINT32_MAX)
        return UINT32_MAX - 1;
    return static_cast<lk_time_t>(temp & 0xffffffff);
}

mx_status_t magenta_sleep(mx_time_t nanoseconds);

// Determines if this handle is to a Resource object.
// Used to provide access to privileged syscalls.
// Later, Resource objects will be finer-grained.
mx_status_t validate_resource_handle(mx_handle_t handle);

// Convenience function to get go from process handle to process.
mx_status_t get_process(ProcessDispatcher* up,
                        mx_handle_t proc_handle,
                        mxtl::RefPtr<ProcessDispatcher>* proc);
