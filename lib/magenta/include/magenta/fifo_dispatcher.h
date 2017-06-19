// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <sys/types.h>

#include <mxtl/ref_counted.h>

class FifoDispatcher : public Dispatcher {
public:
    static status_t Create(uint64_t count, mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~FifoDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_FIFO; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }

    uint64_t count() const { return count_; }
    void GetState(mx_fifo_state_t* out) const;

    status_t AdvanceHead(uint64_t count, mx_fifo_state_t* out);
    status_t AdvanceTail(uint64_t count, mx_fifo_state_t* out);
    status_t SetException(mx_signals_t signal, bool set, mx_fifo_state_t* out);

private:
    explicit FifoDispatcher(uint64_t count);

    // simple RAII class for returning mx_fifo_state_t
    class StateUpdater;

    mutable Mutex lock_;
    const uint64_t count_;
    mx_fifo_state_t state_;
    StateTracker state_tracker_;
};
