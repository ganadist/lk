// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/process_owner_dispatcher.h>

#include <err.h>

constexpr mx_rights_t kDefaultProcessRights = MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_TRANSFER;

status_t ProcessOwnerDispatcher::Create(utils::RefPtr<Dispatcher>* dispatcher,
                                        mx_rights_t* rights,
                                        utils::StringPiece name) {
    utils::unique_ptr<UserProcess> new_process(new UserProcess(name));
    if (!new_process)
        return ERR_NO_MEMORY;

    status_t result = new_process->Initialize();
    if (result != NO_ERROR)
        return result;

    *rights = kDefaultProcessRights;
    *dispatcher = utils::AdoptRef<Dispatcher>(new ProcessOwnerDispatcher(utils::move(new_process)));
    return (*dispatcher) ? NO_ERROR : ERR_NO_MEMORY;
}

ProcessOwnerDispatcher::ProcessOwnerDispatcher(utils::unique_ptr<UserProcess> process)
    : process_(utils::move(process)) {}

ProcessOwnerDispatcher::~ProcessOwnerDispatcher() {}

void ProcessOwnerDispatcher::Close(Handle* handle) {
    process_->Close();
}

Waiter* ProcessOwnerDispatcher::BeginWait(event_t* event, Handle* handle, mx_signals_t signals) {
    return process_->GetWaiter()->BeginWait(event, handle, signals);
}

status_t ProcessOwnerDispatcher::Start(mx_handle_t handle, mx_vaddr_t entry) {
    return process_->Start(reinterpret_cast<void*>(handle), entry);
}

mx_handle_t ProcessOwnerDispatcher::AddHandle(HandleUniquePtr handle) {
    auto hv = process_->MapHandleToValue(handle.get());
    process_->AddHandle(utils::move(handle));
    return hv;
}

status_t ProcessOwnerDispatcher::GetInfo(mx_process_info_t *info) {
    return process_->GetInfo(info);
}

status_t ProcessOwnerDispatcher::SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour) {
    return process_->SetExceptionHandler(handler, behaviour);
}
