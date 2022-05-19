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

namespace shelly {

RGBWController::RGBWController(struct mgos_config_lb *cfg, Output *out_r,
                               Output *out_g, Output *out_b, Output *out_w)
    : LightBulbController(cfg),
      out_r_(out_r),
      out_g_(out_g),
      out_b_(out_b),
      out_w_(out_w) {
}

RGBWController::~RGBWController() {
}

void RGBWController::ReportTransition(const StateRGBW &next,
                                      const StateRGBW &prev) {
  LOG(LL_INFO, ("Output 1: %.2f => %.2f", prev.r, next.r));
  LOG(LL_INFO, ("Output 2: %.2f => %.2f", prev.g, next.g));
  LOG(LL_INFO, ("Output 3: %.2f => %.2f", prev.b, next.b));
  if (out_w_ != nullptr) {
    LOG(LL_INFO, ("Output 4: %.2f => %.2f", prev.w, next.w));
  }
}

void RGBWController::UpdatePWM(const StateRGBW &state) {
  out_r_->SetStatePWM(state.r, "transition");
  out_g_->SetStatePWM(state.g, "transition");
  out_b_->SetStatePWM(state.b, "transition");

  if (out_w_ != nullptr) {
    out_w_->SetStatePWM(state.w, "transition");
  }
}

StateRGBW RGBWController::ConfigToState(
    const struct mgos_config_lb &cfg) const {
  StateRGBW state;
  float h = cfg.hue / 360.0f;
  float s = cfg.saturation / 100.0f;
  float v = cfg.brightness / 100.0f;

  if (cfg.saturation == 0) {
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
  return state;
}

std::string StateRGBW::ToString() const {
  return mgos::SPrintf("[r=%.2f g=%.2f b=%.2f w=%.2f]", r, g, b, w);
}

}  // namespace shelly
