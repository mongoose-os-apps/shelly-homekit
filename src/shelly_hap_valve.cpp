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

#include "shelly_hap_valve.hpp"

#include "mgos_hap_accessory.hpp"

namespace shelly {
namespace hap {

Valve::Valve(int id, Input *in, Output *out, PowerMeter *out_pm, Output *led_out,
           struct mgos_config_sw *cfg)
    : ShellySwitch(id, in, out, out_pm, led_out, cfg) {
}

Valve::~Valve() {
}

Status Valve::Init() {
  auto st = ShellySwitch::Init();
  if (!st.ok()) return st;

  const int id1 = id() - 1;  // IDs used to start at 0, preserve compat.
  uint16_t iid = SHELLY_HAP_IID_BASE_VALVE + (SHELLY_HAP_IID_STEP_VALVE * id1);
  svc_.iid = iid++;
  svc_.serviceType = &kHAPServiceType_Valve;
  svc_.debugDescription = kHAPServiceDebugDescription_Valve;
  // Name
  AddNameChar(iid++, cfg_->name);
  // Active
  auto *active_char = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_Active, 0, 1, 1,
      std::bind(&Valve::HandleActiveRead, this, _1, _2, _3),
      true /* supports_notification */,
      std::bind(&Valve::HandleActiveWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_Active);
  state_notify_chars_.push_back(active_char);
  AddChar(active_char);
  // In Use
  auto *in_use_char = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_InUse, 0, 1, 1,
      std::bind(&Valve::HandleActiveRead, this, _1, _2, _3),
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_InUse);
  state_notify_chars_.push_back(in_use_char);
  AddChar(in_use_char);
  // Valve Type
  auto *valve_type_char = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_ValveType, 0, 1, 1,
      std::bind(&Valve::HandleValveTypeRead, this, _1, _2, _3),
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_ValveType);
  state_notify_chars_.push_back(valve_type_char);
  AddChar(valve_type_char);

  return Status::OK();
}

HAPError Valve::HandleActiveRead(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicReadRequest *request, uint8_t *value) {
  *value = (out_->GetState() ? 1 : 0);
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError Valve::HandleActiveWrite(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicWriteRequest *request, uint8_t value) {
  SetOutputState((value == 1), "HAP");
  state_notify_chars_[1]->RaiseEvent();
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError Valve::HandleValveTypeRead(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicReadRequest *request, uint8_t *value) {
  *value = cfg_->valve_type;
  (void) server;
  (void) request;
  return kHAPError_None;
}

}  // namespace hap
}  // namespace shelly
