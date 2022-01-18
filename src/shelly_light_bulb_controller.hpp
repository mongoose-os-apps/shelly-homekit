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

#pragma once

namespace shelly {

class LightBulbControllerBase {
 public:
  enum class BulbType {
    kBrightness = 0,
    kColortemperature = 1,
    kHueSat = 2,
  };
  typedef std::function<void()> Update;

  LightBulbControllerBase(struct mgos_config_lb *cfg, Update ud);
  LightBulbControllerBase(const LightBulbControllerBase &other) = delete;
  virtual ~LightBulbControllerBase();

  void UpdateOutput();

  virtual BulbType Type() {
    return BulbType::kBrightness;
  }

  bool IsOn() const;
  bool IsOff() const;

 protected:
  struct mgos_config_lb *cfg_;

  Update update_;
};

template <class T>
class LightBulbController : public LightBulbControllerBase {
 public:
  LightBulbController(struct mgos_config_lb *cfg)
      : LightBulbControllerBase(
            cfg,
            std::bind(&LightBulbController<T>::UpdateOutputSpecialized, this)),
        transition_timer_(
            std::bind(&LightBulbController<T>::TransitionTimerCB, this)) {
  }
  LightBulbController(const LightBulbControllerBase &other) = delete;

 protected:
  mgos::Timer transition_timer_;
  int64_t transition_start_ = 0;

  T state_start_;
  T state_now_;
  T state_end_;

  virtual T ConfigToState() = 0;
  virtual void ReportTransition(const T &next, const T &prev) = 0;
  virtual void UpdatePWM(const T &state) = 0;

  void TransitionTimerCB() {
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

  void UpdateOutputSpecialized() {
    state_start_ = state_now_;
    if (IsOn()) {
      state_end_ = ConfigToState();
    } else {
      // turn off
      T statezero;
      state_end_ = statezero;
    }

    LOG(LL_INFO, ("Transition started... %d [ms]", cfg_->transition_time));

    ReportTransition(state_end_, state_start_);

    // restarting transition timer to fade
    transition_start_ = mgos_uptime_micros();
    transition_timer_.Reset(10, MGOS_TIMER_REPEAT);
  }
};
}  // namespace shelly
