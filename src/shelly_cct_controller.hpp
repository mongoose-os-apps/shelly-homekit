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

#include "mgos.hpp"
#include "mgos_hap_chars.hpp"
#include "mgos_timers.hpp"
#include "shelly_light_bulb_controller.hpp"
#include "shelly_output.hpp"

#pragma once

namespace shelly {
class CCTController : public LightBulbController {
 public:
  CCTController(struct mgos_config_lb *cfg, Output *out_ww, Output *out_cw);
  CCTController(const CCTController &other) = delete;
  virtual ~CCTController();

  struct State {
    float ww;
    float cw;
  };

  void UpdateOutput() override;

  BulbType Type() override {
    return BulbType::kColortemperature;
  }

 protected:
  void TransitionTimerCB();
  void ColortemperaturetoWhiteChannels(State &state) const;

  mgos::hap::UInt32Characteristic *colortemperature_characteristic;

  Output *const out_ww_, *const out_cw_;
  mgos::Timer transition_timer_;
  int64_t transition_start_ = 0;
  State state_start_{};
  State state_now_{};
  State state_end_{};
};
}  // namespace shelly