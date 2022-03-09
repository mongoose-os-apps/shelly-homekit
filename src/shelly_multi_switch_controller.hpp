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

#include "mgos_timers.hpp"
#include "shelly_light_bulb_controller.hpp"
#include "shelly_output.hpp"
#include "shelly_pm.hpp"

#pragma once

namespace shelly {

struct StateOn {
  bool on;

  StateOn operator+(const StateOn &other) const {
    return {.on = true};
  }

  StateOn operator*(float a) const {
    return {.on = true};
  }
};

class MultiSwitchController : public LightBulbController<StateOn> {
 public:
  MultiSwitchController(struct mgos_config_lb *cfg, Output *out_1, Output *out_2, PowerMeter *out_pm_1, PowerMeter *out_pm_2);
  MultiSwitchController(const MultiSwitchController &other) = delete;
  ~MultiSwitchController();

  BulbType Type() final {
    return BulbType::kWhite;
  }

 private:
  Output *const out_1_;
  Output *const out_2_;
  PowerMeter *const out_pm_1_;
  PowerMeter *const out_pm_2_;

  bool IsOn();

  StateOn ConfigToState() final;
  void ReportTransition(const StateOn &prev, const StateOn &next) final;
  void UpdatePWM(const StateOn &state) final;
};
}  // namespace shelly
