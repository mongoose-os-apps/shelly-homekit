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

struct StateW {
  float w;

  StateW operator+(const StateW &other) const {
    return {.w = w + other.w};
  }

  StateW operator*(float a) const {
    return {.w = a * w};
  }
};

class WhiteController : public LightBulbController<StateW> {
 public:
  WhiteController(struct mgos_config_lb *cfg, Output *out_w);
  WhiteController(const WhiteController &other) = delete;
  ~WhiteController();

  BulbType Type() final {
    return BulbType::kWhite;
  }

 private:
  Output *const out_w_;

  StateW ConfigToState() final;
  void ReportTransition(const StateW &prev, const StateW &next) final;
  void UpdatePWM(const StateW &state) final;
};
}  // namespace shelly
