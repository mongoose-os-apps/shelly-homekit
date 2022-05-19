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

#include "shelly_cct_controller.hpp"

#include "mgos.hpp"
#include "mgos_hap_chars.hpp"

namespace shelly {

CCTController::CCTController(struct mgos_config_lb *cfg, Output *out_cw,
                             Output *out_ww)
    : LightBulbController<StateCCT>(cfg), out_ww_(out_ww), out_cw_(out_cw) {
}

CCTController::~CCTController() {
}

void CCTController::ReportTransition(const StateCCT &next,
                                     const StateCCT &prev) {
  LOG(LL_INFO, ("Output 1: %.2f => %.2f", prev.ww, next.ww));
  LOG(LL_INFO, ("Output 2: %.2f => %.2f", prev.cw, next.cw));
}

void CCTController::UpdatePWM(const StateCCT &state) {
  out_ww_->SetStatePWM(state.ww, "transition");
  out_cw_->SetStatePWM(state.cw, "transition");
}

StateCCT CCTController::ConfigToState(const struct mgos_config_lb &cfg) const {
  StateCCT state;

  float v = cfg.brightness / 100.0f;
  float temp = cfg.color_temperature;

  // brightness and color temperature [mired] to cw, ww values
  // uses additive mixing, so at middle temp it is 50/50
  int temp_max = 400;
  int temp_min = 50;
  temp -= temp_min;
  temp /= (temp_max - temp_min);
  state.ww = temp * v;
  state.cw = (1.0f - temp) * v;
  return state;
}

std::string StateCCT::ToString() const {
  return mgos::SPrintf("[ww=%.2f cw=%.2f]", ww, cw);
}

}  // namespace shelly
