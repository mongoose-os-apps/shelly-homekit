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

#include "shelly_pm.hpp"

#include <cmath>

#include "mgos.hpp"
#include "mgos_ade7953.h"

namespace shelly {

class ADE7953PowerMeter : public PowerMeter {
 public:
  ADE7953PowerMeter(int id, struct mgos_ade7953 *ade7953, int channel);
  virtual ~ADE7953PowerMeter();

  Status Init() override;
  StatusOr<float> GetPowerW() override;
  StatusOr<float> GetEnergyWH() override;

 private:
  StatusOr<float> GetEnergyWH(bool reset);
  void AEAAccumulateTimerCB();

  struct mgos_ade7953 *const ade7953_;
  const int channel_;
  float aea_acc_ = 0;  // Accumulated active energy.
  mgos::Timer acc_timer_;
};

}  // namespace shelly
