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

#include "shelly_pm.h"

#ifdef MGOS_HAVE_ADE7953

#include <cmath>

#include "mgos.h"
#include "mgos_ade7953.h"

namespace shelly {

struct mgos_ade7953 *s_ade7953 = NULL;

class ADE7953PowerMeter : public PowerMeter {
 public:
  ADE7953PowerMeter(int id, struct mgos_ade7953 *ade7953, int channel)
      : id_(id), ade7953_(ade7953), channel_(channel) {
    acc_timer_id_ =
        mgos_set_timer(10000, MGOS_TIMER_REPEAT,
                       ADE7953PowerMeter::AEAAccumulateTimerCB, this);
  }
  virtual ~ADE7953PowerMeter() {
    mgos_clear_timer(acc_timer_id_);
  }

  int id() const override {
    return id_;
  }

  StatusOr<float> GetPowerW() override {
    float apa = 0;
    if (!mgos_ade7953_get_apower(ade7953_, channel_, &apa)) {
      return mgos::Errorf(STATUS_UNAVAILABLE, "Failed to read %s", "AP");
    }
    apa = std::fabs(apa);
    if (apa < 1) apa = 0;  // Suppress noise.
    return apa;
  }

  StatusOr<float> GetEnergyWH() override {
    return GetEnergyWH(false /* reset */);
  }

 private:
  StatusOr<float> GetEnergyWH(bool reset) {
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

  static void AEAAccumulateTimerCB(void *arg) {
    ADE7953PowerMeter *ade = static_cast<ADE7953PowerMeter *>(arg);
    ade->GetEnergyWH(true /* reset */);
  }

  const int id_;
  struct mgos_ade7953 *const ade7953_;
  const int channel_;
  float aea_acc_ = 0;  // Accumulated active energy.
  mgos_timer_id acc_timer_id_ = MGOS_INVALID_TIMER_ID;
};

StatusOr<std::vector<std::unique_ptr<PowerMeter>>> PowerMeterInit() {
  std::vector<std::unique_ptr<PowerMeter>> res;
  const struct mgos_ade7953_config ade7953_cfg = {
      .voltage_scale = .0000382602,
      .voltage_offset = -0.068,
      .current_scale = {0.00000949523, 0.00000949523},
      .current_offset = {-0.017, -0.017},
      .apower_scale = {(1 / 164.0), (1 / 164.0)},
      .aenergy_scale = {(1 / 25240.0), (1 / 25240.0)},
  };

  s_ade7953 = mgos_ade7953_create(mgos_i2c_get_global(), &ade7953_cfg);

  if (s_ade7953 == nullptr) {
    return mgos::Errorf(STATUS_UNAVAILABLE, "Failed to init ADE7953");
  }

  res.push_back(
      std::unique_ptr<PowerMeter>(new ADE7953PowerMeter(1, s_ade7953, 1)));
  res.push_back(
      std::unique_ptr<PowerMeter>(new ADE7953PowerMeter(2, s_ade7953, 0)));

  return res;
}

}  // namespace shelly
#else
namespace shelly {
StatusOr<std::vector<std::unique_ptr<PowerMeter>>> PowerMeterInit() {
  return std::vector<std::unique_ptr<PowerMeter>>();
}
}  // namespace shelly
#endif
