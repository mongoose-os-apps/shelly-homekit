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

#include "shelly_white_controller.hpp"

#include "mgos.hpp"

namespace shelly {

WhiteController::WhiteController(struct mgos_config_lb *cfg, Output *out_w)
    : LightBulbController<StateW>(cfg), out_w_(out_w) {
}

WhiteController::~WhiteController() {
}

StateW WhiteController::ConfigToState(const struct mgos_config_lb &cfg) const {
  StateW st;
  st.w = (cfg.state ? cfg.brightness / 100.0f : 0.0f);
  return st;
}

void WhiteController::ReportTransition(const StateW &next, const StateW &prev) {
  LOG(LL_INFO, ("Output 1: %.2f => %.2f", prev.w, next.w));
}

void WhiteController::UpdatePWM(const StateW &state) {
  out_w_->SetStatePWM(state.w, "transition");
}

std::string StateW::ToString() const {
  return mgos::SPrintf("[w=%.2f]", w);
}

}  // namespace shelly
