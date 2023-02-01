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

#include "shelly_hap_carbon_dioxide_sensor.hpp"

namespace shelly {
namespace hap {

CarbonDioxideSensor::CarbonDioxideSensor(int id, Input *in,
                                         struct mgos_config_in_sensor *cfg)
    : SensorBase(id, in, cfg, SHELLY_HAP_IID_BASE_CARBON_DIOXIDE_SENSOR,
                 &kHAPServiceType_CarbonDioxideSensor,
                 kHAPServiceDebugDescription_CarbonDioxideSensor) {
}

CarbonDioxideSensor::~CarbonDioxideSensor() {
}

Component::Type CarbonDioxideSensor::type() const {
  return Type::kCarbonDioxideSensor;
}

Status CarbonDioxideSensor::Init() {
  const Status &st = SensorBase::Init();
  if (!st.ok()) return st;
  AddChar(new mgos::hap::UInt8Characteristic(
      svc_.iid + 2, &kHAPCharacteristicType_CarbonDioxideDetected, 0, 1, 1,
      std::bind(&mgos::hap::ReadUInt8<bool>, _1, _2, _3, &state_),
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_CarbonDioxideDetected));
  return Status::OK();
}

}  // namespace hap
}  // namespace shelly
