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

#include "shelly_multi_switch_controller.hpp"

namespace shelly {

MultiSwitchController::MultiSwitchController(struct mgos_config_lb *cfg, Output *out_1, Output *out_2, PowerMeter *out_pm_1, PowerMeter *out_pm_2)
    : LightBulbController<StateOn>(cfg), out_1_(out_1), out_2_(out_2), out_pm_1_(out_pm_1), out_pm_2_(out_pm_2) {
}

MultiSwitchController::~MultiSwitchController() {
}

bool MultiSwitchController::IsOn() {
  LOG(LL_INFO, ("READ IS ON"));
  float o1 = out_pm_1_->GetPowerW().ValueOrDie();
  float o2 = out_pm_2_->GetPowerW().ValueOrDie();
  LOG(LL_INFO, ("PM 1: %1f", o1));
  LOG(LL_INFO, ("PM 2: %1f", o2));

  return (o1 > 0.0) || (o2 > 0.0);
}

StateOn MultiSwitchController::ConfigToState() {
  return {.on = true};
}

void MultiSwitchController::ReportTransition(const StateOn &next, const StateOn &prev) {
  LOG(LL_INFO, ("Output 1: %s, Output 2: %s", OnOff(out_1_->GetState()), OnOff(out_2_->GetState())));
}

void MultiSwitchController::UpdatePWM(const StateOn &state) {
  out_1_->SetState(!state.on, "transition");
  out_2_->SetState(state.on, "transition");
}

}  // namespace shelly
