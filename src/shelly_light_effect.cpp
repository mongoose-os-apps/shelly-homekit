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

#include "shelly_light_effect.hpp"

#include "shelly_light_bulb_controller.hpp"

namespace shelly {

LightEffectBlink::LightEffectBlink(LightBulbControllerBase *bulb,
                                   int interval_ms, int repeat_n)
    : interval_(interval_ms),
      bulb_(bulb),
      repeat_timer_(std::bind(&LightEffectBlink::TimerCB, this)),
      repeat_n_(repeat_n),
      active_(false) {
}

Status LightEffectBlink::Start() {
  mgos_config_lb_set_defaults(&cfg);
  cfg.state = true;
  cfg.transition_time = 0;

  repeat_timer_.Reset(interval_ / 2, MGOS_TIMER_REPEAT | MGOS_TIMER_RUN_NOW);

  return Status::OK();
}

void LightEffectBlink::TimerCB() {
  if (repeat_n_ != 0) {
    active_ = !active_;
    if (active_) {
      if (repeat_n_ > 0) {
        repeat_n_--;
      }
    }
    int brightness = active_ ? 100 : 0;

    cfg.brightness = brightness;
    bulb_->UpdateOutput(&cfg);
  } else {
    cfg.state = false;

    repeat_timer_.Clear();
    bulb_->UpdateOutput(nullptr);  // go back to initial state (with transition)
  }
}
}  // namespace shelly