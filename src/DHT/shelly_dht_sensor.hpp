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

#include "shelly_temp_sensor.hpp"

#include "mgos_timers.hpp"

#include <mgos_dht.h>

#include <vector>

namespace shelly {

std::vector<std::unique_ptr<TempSensor>> DiscoverDHTSensors(int in, int out);

class DHTSensor : public HumidityTempSensor {
 public:
  DHTSensor(uint8_t pin_in, uint8_t pin_out);
  virtual ~DHTSensor();

  Status Init();
  StatusOr<float> GetTemperature() override;
  StatusOr<float> GetHumidity() override;

  virtual void StartUpdating(int interval) override;
  virtual void StopUpdating() override;

 private:
  uint8_t pin_in_;
  uint8_t pin_out_;

  mgos::Timer meas_timer_;

  mgos_dht *dht;

  void UpdateTemperatureCB();
  StatusOr<float> result_;
  StatusOr<float> result_humidity_;
};

}  // namespace shelly
