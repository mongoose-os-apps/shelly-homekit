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

#pragma once

namespace shelly {
struct StateRGBW {
  float r;
  float g;
  float b;
  float w;

  StateRGBW operator+(const StateRGBW &other) const {
    return {
        .r = r + other.r,
        .g = g + other.g,
        .b = b + other.b,
        .w = w + other.w,
    };
  }

  StateRGBW operator*(float a) const {
    return {
        .r = a * r,
        .g = a * g,
        .b = a * b,
        .w = a * w,
    };
  }
};

class RGBWController : public LightBulbController<StateRGBW> {
 public:
  RGBWController(struct mgos_config_lb *cfg, Output *out_r, Output *out_g,
                 Output *out_b, Output *out_w);
  RGBWController(const RGBWController &other) = delete;
  ~RGBWController();

  BulbType Type() final {
    return BulbType::kRGBW;
  }

 private:
  Output *const out_r_, *const out_g_, *const out_b_, *const out_w_;
  StateRGBW ConfigToState() final;
  void ReportTransition(const StateRGBW &next, const StateRGBW &prev) final;
  void UpdatePWM(const StateRGBW &state) final;
};
}  // namespace shelly
