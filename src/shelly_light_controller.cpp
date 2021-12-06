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
    : LightBulbController(cfg),
      out_w_(out_w),
      transition_timer_(std::bind(&LightController::TransitionTimerCB, this)) {
}

LightController::~LightController() {
}

void LightController::UpdateOutput() {
  state_start_ = state_now_;
  if (IsOn()) {
    float v = cfg_->brightness / 100.0f;
    state_end_.w = v;

  } else {
    // turn off
    state_end_.w = 0.0f;
  }

  LOG(LL_INFO, ("Transition started... %d [ms]", cfg_->transition_time));

  LOG(LL_INFO, ("Output 1: %.2f => %.2f", state_start_.w, state_end_.w));

  // restarting transition timer to fade
  transition_start_ = mgos_uptime_micros();
  transition_timer_.Reset(10, MGOS_TIMER_REPEAT);
}

void LightController::TransitionTimerCB() {
  int64_t elapsed = mgos_uptime_micros() - transition_start_;
  int64_t duration = cfg_->transition_time * 1000;

  if (elapsed > duration) {
    transition_timer_.Clear();
    state_now_ = state_end_;
    LOG(LL_INFO, ("Transition ready"));
  } else {
    float alpha = static_cast<float>(elapsed) / static_cast<float>(duration);
    state_now_.w = alpha * state_end_.w + (1 - alpha) * state_start_.w;
  }

  out_w_->SetStatePWM(state_now_.w, "transition");
}

}  // namespace shelly
