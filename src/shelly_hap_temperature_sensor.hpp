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

#include "mgos_hap_service.hpp"
#include "mgos_sys_config.h"

#include "shelly_common.hpp"
#include "shelly_component.hpp"

#include "shelly_temp_sensor.hpp"

namespace shelly {
namespace hap {

class TemperatureSensor : public Component, public mgos::hap::Service {
 public:
  TemperatureSensor(int id, std::unique_ptr<TempSensor> sensor,
                    struct mgos_config_ts *cfg);
  virtual ~TemperatureSensor();

  // Component interface impl.
  Type type() const override;
  std::string name() const override;
  Status Init() override;

  StatusOr<std::string> GetInfo() const override;
  StatusOr<std::string> GetInfoJSON() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;
  Status SetState(const std::string &state_json) override;

 private:
  std::unique_ptr<TempSensor> temp_sensor_;
  struct mgos_config_ts *cfg_;

  mgos::hap::FloatCharacteristic *current_temperature_characteristic_;

  void ValueChanged();
};
}  // namespace hap
}  // namespace shelly
