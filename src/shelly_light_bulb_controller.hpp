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

#pragma once

#include <functional>
#include "mgos_sys_config.h"
#include "mgos_timers.hpp"

namespace shelly {

class LightBulbControllerBase {
 public:
  enum class BulbType {
    kWhite = 0,
    kCCT = 1,
    kRGBW = 2,
    kMax = 3,
  };
  typedef std::function<void(struct mgos_config_lb *cfg_)> Update;

  LightBulbControllerBase(struct mgos_config_lb *cfg, Update ud);
  LightBulbControllerBase(const LightBulbControllerBase &other) = delete;
  virtual ~LightBulbControllerBase();

  void UpdateOutput(struct mgos_config_lb *cfg_) const;

  virtual BulbType Type() = 0;

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
            cfg, std::bind(&LightBulbController<T>::UpdateOutputSpecialized,
                           this, std::placeholders::_1)),
        transition_timer_(
            std::bind(&LightBulbController<T>::TransitionTimerCB, this)) {
  }
  LightBulbController(const LightBulbControllerBase &other) = delete;

 private:
  mgos::Timer transition_timer_;
  int64_t transition_start_ = 0;
  int transition_time_ = 0;

  T state_start_{};
  T state_now_{};
  T state_end_{};

  virtual T ConfigToState(struct mgos_config_lb *cfg) = 0;
  virtual void ReportTransition(const T &next, const T &prev) = 0;
  virtual void UpdatePWM(const T &state) = 0;

  void TransitionTimerCB();
  void UpdateOutputSpecialized(struct mgos_config_lb *cfg);
};
}  // namespace shelly
