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

#include "shelly_light_bulb_controller.hpp"

#include "shelly_cct_controller.hpp"
#include "shelly_rgbw_controller.hpp"
#include "shelly_white_controller.hpp"
#include "shelly_multi_switch_controller.hpp"

namespace shelly {

LightBulbControllerBase::LightBulbControllerBase(struct mgos_config_lb *cfg,
                                                 Update ud)
    : cfg_(cfg), update_(ud) {
}

LightBulbControllerBase::~LightBulbControllerBase() {
}

bool LightBulbControllerBase::IsOn() {
  LOG(LL_INFO, ("Org is on"));
  return cfg_->state != 0;
}

void LightBulbControllerBase::UpdateOutput() {
  if (update_) {
    update_();
  }
}

bool LightBulbControllerBase::IsOff() {
  return !IsOn();
}

template <class T>
void LightBulbController<T>::TransitionTimerCB() {
  int64_t elapsed = mgos_uptime_micros() - transition_start_;
  int64_t duration = cfg_->transition_time * 1000;

  if (elapsed > duration) {
    transition_timer_.Clear();
    state_now_ = state_end_;
    LOG(LL_INFO, ("Transition ready"));
  } else {
    float alpha = static_cast<float>(elapsed) / static_cast<float>(duration);
    state_now_ = state_end_ * alpha + state_start_ * (1 - alpha);
  }

  UpdatePWM(state_now_);
}
template <class T>
void LightBulbController<T>::UpdateOutputSpecialized() {
  state_start_ = state_now_;
  if (IsOn()) {
    state_end_ = ConfigToState();
  } else {
    // turn off
    T statezero{};
    state_end_ = statezero;
  }

  LOG(LL_INFO, ("Transition started... %d [ms]", cfg_->transition_time));

  ReportTransition(state_end_, state_start_);

  // restarting transition timer to fade
  transition_start_ = mgos_uptime_micros();
  transition_timer_.Reset(10, MGOS_TIMER_REPEAT);
}

template class LightBulbController<StateW>;
template class LightBulbController<StateCCT>;
template class LightBulbController<StateRGBW>;
template class LightBulbController<StateOn>;

}  // namespace shelly
