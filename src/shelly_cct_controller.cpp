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
    : LightBulbController(cfg),
      out_ww_(out_ww),
      out_cw_(out_cw),
      transition_timer_(std::bind(&CCTController::TransitionTimerCB, this)) {
}

CCTController::~CCTController() {
}

void CCTController::UpdateOutput() {
  state_start_ = state_now_;
  if (IsOn()) {
    ColortemperaturetoWhiteChannels(state_end_);
  } else {
    // turn off
    state_end_.ww = state_end_.cw = 0.0f;
  }

  LOG(LL_INFO, ("Transition started... %d [ms]", cfg_->transition_time));

  LOG(LL_INFO, ("Output 1: %.2f => %.2f", state_start_.ww, state_end_.ww));
  LOG(LL_INFO, ("Output 2: %.2f => %.2f", state_start_.cw, state_end_.cw));

  // restarting transition timer to fade
  transition_start_ = mgos_uptime_micros();
  transition_timer_.Reset(10, MGOS_TIMER_REPEAT);
}

void CCTController::TransitionTimerCB() {
  int64_t elapsed = mgos_uptime_micros() - transition_start_;
  int64_t duration = cfg_->transition_time * 1000;

  if (elapsed > duration) {
    transition_timer_.Clear();
    state_now_ = state_end_;
    LOG(LL_INFO, ("Transition ready"));
  } else {
    float alpha = static_cast<float>(elapsed) / static_cast<float>(duration);
    state_now_.ww = alpha * state_end_.ww + (1 - alpha) * state_start_.ww;
    state_now_.cw = alpha * state_end_.cw + (1 - alpha) * state_start_.cw;
  }

  out_ww_->SetStatePWM(state_now_.ww, "transition");
  out_cw_->SetStatePWM(state_now_.cw, "transition");
}

void CCTController::ColortemperaturetoWhiteChannels(State &state) const {
  float v = cfg_->brightness / 100.0f;

  // brightness and colortemperature in mired to cw, ww values
  // uses additive mixing, so at middle temp it is 50/50
  int temp_max = 400;
  int temp_min = 50;
  float temp = cfg_->colortemperature;
  temp -= temp_min;
  temp /= (temp_max - temp_min);
  state.ww = temp * v;
  state.cw = (1.0f - temp) * v;
}
}  // namespace shelly
