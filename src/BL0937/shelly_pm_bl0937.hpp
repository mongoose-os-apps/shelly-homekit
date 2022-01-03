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

#include "shelly_pm.hpp"

#include "mgos_timers.hpp"

namespace shelly {

class BL0937PowerMeter : public PowerMeter {
 public:
  BL0937PowerMeter(int id, int cf_pin, int cf1_pin, int sel_pin, int meas_time,
                   float apc);
  virtual ~BL0937PowerMeter();

  Status Init() override;
  StatusOr<float> GetPowerW() override;
  StatusOr<float> GetEnergyWH() override;

 private:
  static void GPIOIntHandler(int pin, void *arg);
  void MeasureTimerCB();

  const int cf_pin_, cf1_pin_, sel_pin_, meas_time_;
  const float apc_;

  volatile uint32_t cf_count_ = 0, cf1_count_ = 0;
  int64_t meas_start_ = 0;

  float apa_ = 0;  // Last active power reading, W.
  float aea_ = 0;  // Accumulated active energy, Wh.

  mgos::Timer meas_timer_;
};

}  // namespace shelly
