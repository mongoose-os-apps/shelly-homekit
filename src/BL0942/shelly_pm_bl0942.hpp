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

class BL0942PowerMeter : public PowerMeter {
 public:
  BL0942PowerMeter(int id, int tx_pin, int rx_pin, int meas_time, int uart_no);
  virtual ~BL0942PowerMeter();

  Status Init() override;
  StatusOr<float> GetPowerW() override;
  StatusOr<float> GetEnergyWH() override;

 private:
  void MeasureTimerCB();

  const int tx_pin_, rx_pin_, meas_time_, uart_no_;

  float apa_ = 0;  // Last active power reading, W.
  float aea_ = 0;  // Accumulated active energy, Wh.
                   //
  bool ReadReg(uint8_t reg, uint8_t *rx_buf, size_t len);
  bool WriteReg(uint8_t reg, uint32_t val);

  mgos::Timer meas_timer_;
};

}  // namespace shelly
