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

#include "shelly_hap_outlet.hpp"

namespace shelly {
namespace hap {

Outlet::Outlet(int id, Input *in, Output *out, PowerMeter *out_pm,
               Output *led_out, struct mgos_config_sw *cfg)
    : ShellySwitch(id, in, out, out_pm, led_out, cfg) {
}

Outlet::~Outlet() {
}

Status Outlet::Init() {
  auto st = ShellySwitch::Init();
  if (!st.ok()) return st;

  const int id1 = id() - 1;  // IDs used to start at 0, preserve compat.
  uint16_t iid =
      SHELLY_HAP_IID_BASE_OUTLET + (SHELLY_HAP_IID_STEP_OUTLET * id1);
  svc_.iid = iid++;
  svc_.serviceType = &kHAPServiceType_Outlet;
  svc_.debugDescription = kHAPServiceDebugDescription_Outlet;
  // Name
  AddNameChar(iid++, cfg_->name);
  // On
  auto *on_char = new mgos::hap::BoolCharacteristic(
      iid++, &kHAPCharacteristicType_On,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicReadRequest *,
             bool *value) {
        *value = out_->GetState();
        return kHAPError_None;
      },
      true /* supports_notification */,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicWriteRequest *,
             bool value) {
        SetOutputState(value, "HAP");
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_On);
  state_notify_chars_.push_back(on_char);
  AddChar(on_char);
  // Outlet In Use
  AddChar(new mgos::hap::BoolCharacteristic(
      iid++, &kHAPCharacteristicType_OutletInUse,
      [](HAPAccessoryServerRef *, const HAPBoolCharacteristicReadRequest *,
         bool *value) {
        *value = true;
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_OutletInUse));

  return Status::OK();
}

}  // namespace hap
}  // namespace shelly
