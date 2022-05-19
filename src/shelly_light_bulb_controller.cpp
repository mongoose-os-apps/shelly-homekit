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

namespace shelly {

LightBulbControllerBase::LightBulbControllerBase(struct mgos_config_lb *cfg,
                                                 UpdateFn ud)
    : cfg_(cfg), update_(ud) {
}

LightBulbControllerBase::~LightBulbControllerBase() {
}

void LightBulbControllerBase::UpdateOutput(struct mgos_config_lb *cfg,
                                           bool cancel_previous) const {
  if (update_) {
    update_(*cfg, cancel_previous);
  }
}

bool LightBulbControllerBase::IsOn() const {
  return cfg_->state != 0;
}

bool LightBulbControllerBase::IsOff() const {
  return cfg_->state == 0;
}

template <class T>
void LightBulbController<T>::TransitionTimerCB() {
  const auto &cur = transitions_.front();
  int64_t elapsed = mgos_uptime_micros() - transition_start_;

  if (elapsed >= cur.transition_time_micros) {
    LOG(LL_INFO, ("Transition finished"));
    state_now_ = cur.state_end;
    transitions_.pop_front();
    transition_timer_.Clear();
    StartPendingTransitions();
  } else {
    float alpha = static_cast<float>(elapsed) /
                  static_cast<float>(cur.transition_time_micros);
    state_now_ = cur.state_end * alpha + state_start_ * (1 - alpha);
  }

  UpdatePWM(state_now_);
}

template <class T>
void LightBulbController<T>::UpdateOutputSpecialized(
    const struct mgos_config_lb &cfg, bool cancel_previous) {
  if (cancel_previous) {
    transitions_.clear();
  }

  Transition<T> t;
  t.state_end = ConfigToState(cfg);
  t.transition_time_micros = cfg.transition_time * 1000;
  transitions_.emplace_back(t);

  StartPendingTransitions();
}

template <class T>
void LightBulbController<T>::StartPendingTransitions() {
  if (transition_timer_.IsValid() || transitions_.empty()) {
    // already running or no further transitions queued
    return;
  }

  const auto &cur = transitions_.front();
  state_start_ = state_now_;

  // restarting transition timer to fade
  transition_start_ = mgos_uptime_micros();
  transition_timer_.Reset(10, MGOS_TIMER_REPEAT);

  LOG(LL_INFO,
      ("Starting transition: %s -> %s, %lld ms",
       state_start_.ToString().c_str(), cur.state_end.ToString().c_str(),
       (long long) cur.transition_time_micros / 1000));

  ReportTransition(cur.state_end, state_start_);
}

template class LightBulbController<StateW>;
template class LightBulbController<StateCCT>;
template class LightBulbController<StateRGBW>;

}  // namespace shelly
