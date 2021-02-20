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

#include "shelly_mock_pm.hpp"

namespace shelly {

MockPowerMeter::MockPowerMeter(int id)
    : PowerMeter(id),
      meas_timer_(std::bind(&MockPowerMeter::MeasureTimerCB, this)) {
}

MockPowerMeter::~MockPowerMeter() {
}

Status MockPowerMeter::Init() {
  meas_timer_.Reset(1000, MGOS_TIMER_REPEAT);
  return Status::OK();
}

StatusOr<float> MockPowerMeter::GetPowerW() {
  return apa_;
}

StatusOr<float> MockPowerMeter::GetEnergyWH() {
  return aea_;
}

void MockPowerMeter::SetPowerW(float w) {
  LOG(LL_INFO, ("PM %d W %.2f -> %.2f", id(), apa_, w));
  apa_ = w;
}

void MockPowerMeter::SetEnergyWH(float wh) {
  LOG(LL_INFO, ("PM %d WH %.2f -> %.2f", id(), aea_, wh));
  aea_ = wh;
}

void MockPowerMeter::MeasureTimerCB() {
  aea_ += (apa_ / 3600);
}

}  // namespace shelly
