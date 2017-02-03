// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <lib/ktrace.h>

#include <magenta/handle_owner.h>
#include <magenta/magenta.h>
#include <magenta/port_dispatcher.h>
#include <magenta/port_dispatcher_v2.h>
#include <magenta/process_dispatcher.h>
#include <magenta/user_copy.h>

#include <mxtl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_port_create(uint32_t options, mx_handle_t* _out) {
    LTRACEF("options %u\n", options);

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;

    mx_status_t result = (options == MX_PORT_OPT_V2) ?
        PortDispatcherV2::Create(options, &dispatcher, &rights):
        PortDispatcher::Create(options, &dispatcher, &rights);

    if (result != NO_ERROR)
        return result;

    uint32_t koid = (uint32_t)dispatcher->get_koid();

    HandleOwner handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();

    mx_handle_t hv = up->MapHandleToValue(handle);

    if (make_user_ptr(_out).copy_to_user(hv) != NO_ERROR)
        return ERR_INVALID_ARGS;
    up->AddHandle(mxtl::move(handle));

    ktrace(TAG_PORT_CREATE, koid, 0, 0, 0);
    return NO_ERROR;
}

static mx_status_t sys_port_queue2(mx_handle_t handle, const void* _packet) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcherV2> port;
    mx_status_t status = up->GetDispatcher(handle, &port, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    AllocChecker ac;
    mxtl::unique_ptr<PortPacket> pp(new (&ac) PortPacket(true));
    if (!ac.check())
        return ERR_NO_MEMORY;

    if (make_user_ptr(_packet).copy_array_from_user(&pp->packet, sizeof(pp->packet)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    pp->packet.type = MX_PKT_TYPE_USER;

    return port->Queue(pp.release());
}

mx_status_t sys_port_queue(mx_handle_t handle, const void* _packet, size_t size) {
    LTRACEF("handle %d\n", handle);

    if (size > MX_PORT_MAX_PKT_SIZE)
        return ERR_BUFFER_TOO_SMALL;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> port;
    mx_status_t status = up->GetDispatcher(handle, &port, MX_RIGHT_WRITE);
    if (status != NO_ERROR) {
        return (size == 0u) ? sys_port_queue2(handle, _packet) : status;
    }

    if (size < sizeof(mx_packet_header_t))
        return ERR_INVALID_ARGS;

    auto iopk = IOP_Packet::MakeFromUser(_packet, size);
    if (!iopk)
        return ERR_NO_MEMORY;

    ktrace(TAG_PORT_QUEUE, (uint32_t)port->get_koid(), (uint32_t)size, 0, 0);

    return port->Queue(iopk);
}

mx_status_t sys_port_wait2(mx_handle_t handle, mx_time_t timeout, void* _packet) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcherV2> port;
    mx_status_t status = up->GetDispatcher(handle, &port, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    PortPacket* pp = nullptr;
    mx_status_t st = port->DeQueue(timeout, &pp);
    if (st != NO_ERROR)
        return st;

    if (make_user_ptr(_packet).copy_array_to_user(&pp->packet, sizeof(pp->packet)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    if (pp->from_heap)
        delete pp;

    return NO_ERROR;
}

mx_status_t sys_port_wait(mx_handle_t handle, mx_time_t timeout,
                          void* _packet, size_t size) {
    LTRACEF("handle %d\n", handle);

    if (!_packet)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> port;
    mx_status_t status = up->GetDispatcher(handle, &port, MX_RIGHT_READ);
    if (status != NO_ERROR) {
        return (size == 0u) ? sys_port_wait2(handle, timeout, _packet) : status;
    }

    ktrace(TAG_PORT_WAIT, (uint32_t)port->get_koid(), 0, 0, 0);

    IOP_Packet* iopk = nullptr;
    status = port->Wait(timeout, &iopk);

    ktrace(TAG_PORT_WAIT_DONE, (uint32_t)port->get_koid(), status, 0, 0);
    if (status < 0)
        return status;

    if (!iopk->CopyToUser(_packet, &size))
        return ERR_INVALID_ARGS;

    IOP_Packet::Delete(iopk);
    return NO_ERROR;
}

mx_status_t sys_port_bind(mx_handle_t handle, uint64_t key,
                          mx_handle_t source, mx_signals_t signals) {
    LTRACEF("handle %d source %d\n", handle, source);

    if (!signals)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PortDispatcher> port;
    mx_status_t status = up->GetDispatcher(handle, &port, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<Dispatcher> source_disp;

    status = up->GetDispatcher(source, &source_disp, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    AllocChecker ac;
    mxtl::unique_ptr<PortClient> client(
        new (&ac) PortClient(mxtl::move(port), key, signals));
    if (!ac.check())
        return ERR_NO_MEMORY;

    return source_disp->set_port_client(mxtl::move(client));
}
