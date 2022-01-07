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

#include "shelly_hap_occupancy_sensor.hpp"

namespace shelly {
namespace hap {

OccupancySensor::OccupancySensor(int id, Input *in,
                                 struct mgos_config_in_sensor *cfg)
    : SensorBase(id, in, cfg, SHELLY_HAP_IID_BASE_OCCUPANCY_SENSOR,
                 &kHAPServiceType_OccupancySensor,
                 kHAPServiceDebugDescription_OccupancySensor) {
}

OccupancySensor::~OccupancySensor() {
}

Component::Type OccupancySensor::type() const {
  return Type::kOccupancySensor;
}

Status OccupancySensor::Init() {
  const Status &st = SensorBase::Init();
  if (!st.ok()) return st;
  AddChar(new mgos::hap::BoolCharacteristic(
      svc_.iid + 2, &kHAPCharacteristicType_OccupancyDetected,
      std::bind(&mgos::hap::ReadBool<bool>, _1, _2, _3, &state_),
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_OccupancyDetected));
  return Status::OK();
}

}  // namespace hap
}  // namespace shelly
