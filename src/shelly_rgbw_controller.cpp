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

#include "shelly_rgbw_controller.hpp"
#include "mgos.hpp"
#include "mgos_hap_chars.hpp"
#include "mgos_hap_service.hpp"
#include "mgos_sys_config.h"
#include "mgos_system.hpp"
#include "mgos_timers.hpp"

namespace shelly {

RGBWController::RGBWController(struct mgos_config_lb *cfg, Output *out_r,
                               Output *out_g, Output *out_b, Output *out_w)
    : out_r_(out_r),
      out_g_(out_g),
      out_b_(out_b),
      out_w_(out_w),
      transition_timer_(std::bind(&RGBWController::TransitionTimerCB, this)) {
  cfg_ = cfg;
}

void RGBWController::UpdateOutput() {
  state_start_ = state_now_;
  if (IsOn()) {
    HSVtoRGBW(state_end_);
  } else {
    // turn off
    state_end_.r = state_end_.g = state_end_.b = state_end_.w = 0.0f;
  }

  LOG(LL_INFO, ("Transition started... %d [ms]", cfg_->transition_time));

  LOG(LL_INFO, ("Output 1: %.2f => %.2f", state_start_.r, state_end_.r));
  LOG(LL_INFO, ("Output 2: %.2f => %.2f", state_start_.g, state_end_.g));
  LOG(LL_INFO, ("Output 3: %.2f => %.2f", state_start_.b, state_end_.b));
  if (out_w_ != nullptr) {
    LOG(LL_INFO, ("Output 4: %.2f => %.2f", state_start_.w, state_end_.w));
  }

  // restarting transition timer to fade
  transition_start_ = mgos_uptime_micros();
  transition_timer_.Reset(10, MGOS_TIMER_REPEAT);
}

void RGBWController::TransitionTimerCB() {
  int64_t elapsed = mgos_uptime_micros() - transition_start_;
  int64_t duration = cfg_->transition_time * 1000;

  if (elapsed > duration) {
    transition_timer_.Clear();
    state_now_ = state_end_;
    LOG(LL_INFO, ("Transition ready"));
  } else {
    float alpha = static_cast<float>(elapsed) / static_cast<float>(duration);
    state_now_.r = alpha * state_end_.r + (1 - alpha) * state_start_.r;
    state_now_.g = alpha * state_end_.g + (1 - alpha) * state_start_.g;
    state_now_.b = alpha * state_end_.b + (1 - alpha) * state_start_.b;
    state_now_.w = alpha * state_end_.w + (1 - alpha) * state_start_.w;
  }

  out_r_->SetStatePWM(state_now_.r, "transition");
  out_g_->SetStatePWM(state_now_.g, "transition");
  out_b_->SetStatePWM(state_now_.b, "transition");

  if (out_w_ != nullptr) {
    out_w_->SetStatePWM(state_now_.w, "transition");
  }
}

void RGBWController::HSVtoRGBW(State &state) const {
  float h = cfg_->hue / 360.0f;
  float s = cfg_->saturation / 100.0f;
  float v = cfg_->brightness / 100.0f;

  if (cfg_->saturation == 0) {
    // if saturation is zero than all rgb channels same as brightness
    state.r = state.g = state.b = v;
  } else {
    // otherwise calc rgb from hsv (hue, saturation, brightness)
    int i = static_cast<int>(h * 6);
    float f = (h * 6.0f - i);
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);

    switch (i % 6) {
      case 0:  // 0° ≤ h < 60°
        state.r = v;
        state.g = t;
        state.b = p;
        break;

      case 1:  // 60° ≤ h < 120°
        state.r = q;
        state.g = v;
        state.b = p;
        break;

      case 2:  // 120° ≤ h < 180°
        state.r = p;
        state.g = v;
        state.b = t;
        break;

      case 3:  // 180° ≤ h < 240°
        state.r = p;
        state.g = q;
        state.b = v;
        break;

      case 4:  // 240° ≤ h < 300°
        state.r = t;
        state.g = p;
        state.b = v;
        break;

      case 5:  // 300° ≤ h < 360°
        state.r = v;
        state.g = p;
        state.b = q;
        break;
    }
  }

  if (out_w_ != nullptr) {
    // apply white channel to rgb if available
    state.w = std::min(state.r, std::min(state.g, state.b));
    state.r = state.r - state.w;
    state.g = state.g - state.w;
    state.b = state.b - state.w;
  } else {
    // otherwise turn white channel off
    state.w = 0.0f;
  }
}
}  // namespace shelly