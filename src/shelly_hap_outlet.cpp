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

#include "shelly_hap_outlet.h"

namespace shelly {
namespace hap {

Outlet::Outlet(int id, Input *in, Output *out, PowerMeter *out_pm,
               struct mgos_config_sw *cfg, HAPAccessoryServerRef *server,
               const HAPAccessory *accessory)
    : ShellySwitch(id, in, out, out_pm, cfg, server, accessory) {
}

Outlet::~Outlet() {
}

Status Outlet::Init() {
  auto st = ShellySwitch::Init();
  if (!st.ok()) return st;

  const int id1 = id() - 1;  // IDs used to start at 0, preserve compat.
  uint16_t iid = IID_BASE_OUTLET + (IID_STEP_OUTLET * id1);
  svc_.iid = iid++;
  svc_.serviceType = &kHAPServiceType_Outlet;
  svc_.debugDescription = kHAPServiceDebugDescription_Outlet;
  // Name
  std::unique_ptr<hap::Characteristic> name_char(new StringCharacteristic(
      iid++, &kHAPCharacteristicType_Name, 64, cfg_->name,
      kHAPCharacteristicDebugDescription_Name));
  hap_chars_.push_back(name_char->GetBase());
  chars_.emplace_back(std::move(name_char));
  // On
  std::unique_ptr<hap::Characteristic> on_char(new BoolCharacteristic(
      iid++, &kHAPCharacteristicType_On,
      std::bind(&Outlet::HandleOnRead, this, _1, _2, _3),
      true /* supports_notification */,
      std::bind(&Outlet::HandleOnWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_On));
  hap_chars_.push_back(on_char->GetBase());
  state_notify_char_ = on_char->GetBase();
  chars_.emplace_back(std::move(on_char));
  // In Use
  std::unique_ptr<hap::Characteristic> in_use_char(new BoolCharacteristic(
      iid++, &kHAPCharacteristicType_OutletInUse,
      std::bind(&Outlet::HandleInUseRead, this, _1, _2, _3),
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_OutletInUse));
  hap_chars_.push_back(in_use_char->GetBase());
  chars_.emplace_back(std::move(in_use_char));

  hap_chars_.push_back(nullptr);
  svc_.characteristics = hap_chars_.data();

  return Status::OK();
}

HAPError Outlet::HandleOnRead(HAPAccessoryServerRef *server,
                              const HAPBoolCharacteristicReadRequest *request,
                              bool *value) {
  *value = out_->GetState();
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError Outlet::HandleOnWrite(HAPAccessoryServerRef *server,
                               const HAPBoolCharacteristicWriteRequest *request,
                               bool value) {
  SetState(value, "HAP");
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError Outlet::HandleInUseRead(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicReadRequest *request, bool *value) {
  *value = true;
  (void) server;
  (void) request;
  return kHAPError_None;
}

}  // namespace hap
}  // namespace shelly
