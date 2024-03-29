/*
 * Copyright (c) Shelly-HomeKit Contributors
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "shelly_hap_lock.hpp"

#include "mgos_hap_accessory.hpp"

namespace shelly {
namespace hap {

Lock::Lock(int id, Input *in, Output *out, PowerMeter *out_pm, Output *led_out,
           struct mgos_config_sw *cfg)
    : ShellySwitch(id, in, out, out_pm, led_out, cfg) {
}

Lock::~Lock() {
}

Status Lock::Init() {
  auto st = ShellySwitch::Init();
  if (!st.ok()) return st;

  const int id1 = id() - 1;  // IDs used to start at 0, preserve compat.
  uint16_t iid = SHELLY_HAP_IID_BASE_LOCK + (SHELLY_HAP_IID_STEP_LOCK * id1);
  svc_.iid = iid++;
  svc_.serviceType = &kHAPServiceType_LockMechanism;
  svc_.debugDescription = kHAPServiceDebugDescription_LockMechanism;
  // Name
  AddNameChar(iid++, cfg_->name);
  // Current State
  auto *cur_state_char = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_LockCurrentState, 0, 3, 1,
      std::bind(&Lock::HandleCurrentStateRead, this, _1, _2, _3),
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_LockCurrentState);
  state_notify_chars_.push_back(cur_state_char);
  AddChar(cur_state_char);
  // Target State
  auto *tgt_state_char = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_LockTargetState, 0, 3, 1,
      std::bind(&Lock::HandleCurrentStateRead, this, _1, _2, _3),
      true /* supports_notification */,
      [this](HAPAccessoryServerRef *server UNUSED_ARG,
             const HAPUInt8CharacteristicWriteRequest *request UNUSED_ARG,
             uint8_t value) {
        SetOutputState((value == 0), "HAP");
        state_notify_chars_[1]->RaiseEvent();
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_LockTargetState);
  state_notify_chars_.push_back(tgt_state_char);
  AddChar(tgt_state_char);

  return Status::OK();
}

HAPError Lock::HandleCurrentStateRead(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicReadRequest *request, uint8_t *value) {
  *value = (out_->GetState() ? 0 : 1);
  (void) server;
  (void) request;
  return kHAPError_None;
}

}  // namespace hap
}  // namespace shelly
