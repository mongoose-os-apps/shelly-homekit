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

#include "shelly_pm_ade7953.hpp"

#include <cmath>

#include "mgos.hpp"
#include "mgos_ade7953.h"

namespace shelly {

struct mgos_ade7953 *s_ade7953 = NULL;

ADE7953PowerMeter::ADE7953PowerMeter(int id, struct mgos_ade7953 *ade7953,
                                     int channel)
    : PowerMeter(id),
      ade7953_(ade7953),
      channel_(channel),
      acc_timer_(std::bind(&ADE7953PowerMeter::AEAAccumulateTimerCB, this)) {
}

ADE7953PowerMeter::~ADE7953PowerMeter() {
}

Status ADE7953PowerMeter::Init() {
  acc_timer_.Reset(10000, MGOS_TIMER_REPEAT);
  return Status::OK();
}

StatusOr<float> ADE7953PowerMeter::GetPowerW() {
  float apa = 0;
  if (!mgos_ade7953_get_apower(ade7953_, channel_, &apa)) {
    return mgos::Errorf(STATUS_UNAVAILABLE, "Failed to read %s", "AP");
  }
  apa = std::fabs(apa);
  if (apa < 1) apa = 0;  // Suppress noise.
  return apa;
}

StatusOr<float> ADE7953PowerMeter::GetEnergyWH() {
  return GetEnergyWH(false /* reset */);
}

StatusOr<float> ADE7953PowerMeter::GetEnergyWH(bool reset) {
  float aea = 0;
  if (!mgos_ade7953_get_aenergy(ade7953_, channel_, reset, &aea)) {
    return mgos::Errorf(STATUS_UNAVAILABLE, "Failed to read %s", "AE");
  }
  float res = aea_acc_ + std::fabs(aea);
  if (reset) {
    aea_acc_ = res;
  }
  return res;
}

void ADE7953PowerMeter::AEAAccumulateTimerCB() {
  GetEnergyWH(true /* reset */);
}

}  // namespace shelly
