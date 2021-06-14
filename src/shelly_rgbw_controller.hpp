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
class RGBWController : public LightBulbController {
 public:
  RGBWController(struct mgos_config_lb *cfg, Output *out_r, Output *out_g,
                 Output *out_b, Output *out_w);
  RGBWController(const RGBWController &other) = delete;
  virtual ~RGBWController();

  struct State {
    float r;
    float g;
    float b;
    float w;
  };

  void UpdateOutput() override;

 protected:
  void TransitionTimerCB();
  void HSVtoRGBW(State &state) const;

  Output *const out_r_, *const out_g_, *const out_b_, *const out_w_;
  mgos::Timer transition_timer_;
  int64_t transition_start_ = 0;
  State state_start_{};
  State state_now_{};
  State state_end_{};
};
}  // namespace shelly
