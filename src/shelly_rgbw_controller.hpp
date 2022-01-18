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
  float r = 0;
  float g = 0;
  float b = 0;
  float w = 0;

  StateRGBW operator+(const StateRGBW &other) {
    StateRGBW s;
    s.r = r + other.r;
    s.g = r + other.g;
    s.b = r + other.b;
    s.w = r + other.w;
    return s;
  }

  StateRGBW operator*(const float a) const {
    StateRGBW s;
    s.r = a * r;
    s.g = a * g;
    s.b = a * b;
    s.w = a * w;
    return s;
  }
};
class RGBWController : public LightBulbController<StateRGBW> {
 public:
  RGBWController(struct mgos_config_lb *cfg, Output *out_r, Output *out_g,
                 Output *out_b, Output *out_w);
  RGBWController(const RGBWController &other) = delete;
  virtual ~RGBWController();

  virtual StateRGBW ConfigToState() override;
  virtual void ReportTransition(const StateRGBW &next,
                                const StateRGBW &prev) override;
  virtual void UpdatePWM(const StateRGBW &state) override;

  BulbType Type() override {
    return BulbType::kHueSat;
  }

 protected:
  Output *const out_r_, *const out_g_, *const out_b_, *const out_w_;
};
}  // namespace shelly
