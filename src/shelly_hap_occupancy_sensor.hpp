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

#pragma once

#include "shelly_hap_sensor_base.hpp"

namespace shelly {
namespace hap {

class OccupancySensor : public SensorBase {
 public:
  OccupancySensor(int id, Input *in, struct mgos_config_in_sensor *cfg);
  virtual ~OccupancySensor();

  // Component interface impl.
  Status Init() override;
  virtual Type type() const override;
};

}  // namespace hap
}  // namespace shelly
