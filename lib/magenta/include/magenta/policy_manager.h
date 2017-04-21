// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <magenta/types.h>

#include <mxtl/ref_ptr.h>

struct mx_policy_basic;
class PortDispatcher;

typedef uint64_t pol_cookie_t;
constexpr pol_cookie_t kPolicyEmpty = 0u;

enum PolicyAction {
    POL_ACTION_ALLOW,
    POL_ACTION_DENY,
    POL_ACTION_KILL
};

// PolicyManager is in charge of providing a space-efficient encoding of
// the external policy as defined in the policy.h public header which
// the client expresses as an array of mx_policy_basic elements.
//
// For example:
//
//   mx_policy_basic in_policy[] = {
//      { MX_BAD_HANDLE_POLICY, MX_POL_TERMINATE },
//      { MX_CREATION_POLICY, MX_POL_CHANNEL_ALLOW },
//      { MX_CREATION_POLICY, MX_POL_FIFO_ALLOW | MX_POL_GENERATE_ALARM },
//      { MX_VMAR_MAP_POLICY, MX_POL_WX_MAP_DENY | MX_POL_TERMINATE }}
//
//  Which is 64 bytes but PolicyManager can encode it in the pol_cookie_t
//  itself if it is a simple policy.

class PolicyManager {
public:
    // Creates in the heap a policy manager with a |default_action|
    // which is returned when QueryBasicPolicy() matches no known condition.
    static PolicyManager* Create(PolicyAction default_action);

    // Creates a |new_policy| based on an |existing_policy| or based on
    // kPolicyEmpty and an array of |policy_input|. When done with the
    // new policy RemovePolicy() must be called.
    //
    // |mode| can be:
    // - MX_JOB_POL_RELATIVE which creates a new policy that only uses
    //   the |policy_input| entries that are unespecified in |existing_policy|
    // - MX_JOB_POL_ABSOLUTE which creates a new policy that requires
    //   that all |policy_input| entries are used.
    //
    // This call can fail in low memory cases and when the |existing_policy|
    // and the policy_input are in conflict given the |mode| paramater.
    mx_status_t AddPolicy(
        uint32_t mode, pol_cookie_t existing_policy,
        const mx_policy_basic* policy_input, size_t policy_count,
        pol_cookie_t* new_policy);

    // Makes a copy of |policy| and must be matched by a RemovePolicy()
    // when done with the policy.
    pol_cookie_t ClonePolicy(pol_cookie_t policy);
    void RemovePolicy(pol_cookie_t policy);

    // Query policy. Given a |policy| generated by AddPolicy() and
    // a |condition| from the  MX_xxxxx_POLICY set, it returns either
    // 'allow', 'deny' or 'kill' and optionally might queue an alarm
    // packet on |alarm_port|.
    //
    // TODO(cpu): Define the alarm packet.
    PolicyAction QueryBasicPolicy(
        pol_cookie_t policy, uint32_t condition, PortDispatcher* alarm_port);

private:
    PolicyManager(PolicyAction default_action_);
    ~PolicyManager() = default;

    const PolicyAction default_action_;
};
