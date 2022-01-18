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

#include "shelly_light_controller.hpp"

namespace shelly {

LightController::LightController(struct mgos_config_lb *cfg, Output *out_w)
    : LightBulbController<StateW>(cfg), out_w_(out_w) {
}

LightController::~LightController() {
}

StateW LightController::ConfigToState() {
  StateW state;
  float v = cfg_->brightness / 100.0f;
  state.w = v;
  return state;
}

void LightController::ReportTransition(const StateW &next, const StateW &prev) {
  LOG(LL_INFO, ("Output 1: %.2f => %.2f", prev.w, next.w));
}

void LightController::UpdatePWM(const StateW &state) {
  out_w_->SetStatePWM(state.w, "transition");
}

}  // namespace shelly
