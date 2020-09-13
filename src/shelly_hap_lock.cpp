/*
 * Copyright (c) 2020 Deomid "rojer" Ryabkov
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

#include "shelly_hap_lock.h"

#define IID_BASE_LOCK 0x300
#define IID_STEP_LOCK 4

namespace shelly {
namespace hap {

Lock::Lock(int id, Input *in, Output *out, PowerMeter *out_pm,
           struct mgos_config_sw *cfg, HAPAccessoryServerRef *server,
           const HAPAccessory *accessory)
    : ShellySwitch(id, in, out, out_pm, cfg, server, accessory) {
}

Lock::~Lock() {
}

Status Lock::Init() {
  auto st = ShellySwitch::Init();
  if (!st.ok()) return st;

  const int id1 = id() - 1;  // IDs used to start at 0, preserve compat.
  uint16_t iid = IID_BASE_LOCK + (IID_STEP_LOCK * id1);
  svc_.iid = iid++;
  svc_.serviceType = &kHAPServiceType_LockMechanism;
  svc_.debugDescription = kHAPServiceDebugDescription_LockMechanism;
  // Name
  std::unique_ptr<hap::Characteristic> name_char(new StringCharacteristic(
      iid++, &kHAPCharacteristicType_Name, 64, cfg_->name,
      kHAPCharacteristicDebugDescription_Name));
  hap_chars_.push_back(name_char->GetBase());
  chars_.emplace_back(std::move(name_char));
  // Current State
  std::unique_ptr<hap::Characteristic> cur_state_char(new UInt8Characteristic(
      iid++, &kHAPCharacteristicType_LockCurrentState, 0, 3, 1,
      std::bind(&Lock::HandleCurrentStateRead, this, _1, _2, _3),
      nullptr,  // write_handler
      kHAPCharacteristicDebugDescription_LockCurrentState));
  hap_chars_.push_back(cur_state_char->GetBase());
  state_notify_char_ = cur_state_char->GetBase();
  chars_.emplace_back(std::move(cur_state_char));
  // Target State
  std::unique_ptr<hap::Characteristic> tgt_state_char(new UInt8Characteristic(
      iid++, &kHAPCharacteristicType_LockTargetState, 0, 3, 1,
      std::bind(&Lock::HandleCurrentStateRead, this, _1, _2, _3),
      std::bind(&Lock::HandleTargetStateWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_LockTargetState));
  hap_chars_.push_back(tgt_state_char->GetBase());
  tgt_state_notify_char_ = tgt_state_char->GetBase();
  chars_.emplace_back(std::move(tgt_state_char));

  hap_chars_.push_back(nullptr);
  svc_.characteristics = hap_chars_.data();

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

HAPError Lock::HandleTargetStateWrite(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicWriteRequest *request, uint8_t value) {
  SetState((value == 0), "HAP");
  HAPAccessoryServerRaiseEvent(server_, tgt_state_notify_char_, &svc_,
                               accessory_);
  (void) server;
  (void) request;
  return kHAPError_None;
}

}  // namespace hap
}  // namespace shelly
