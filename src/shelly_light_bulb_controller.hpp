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

#include <deque>
#include <functional>

#include "mgos_sys_config.h"
#include "mgos_timers.hpp"

#include "shelly_common.hpp"

namespace shelly {

class LightBulbControllerBase {
 public:
  enum class BulbType {
    kWhite = 0,
    kCCT = 1,
    kRGBW = 2,
    kMax = 3,
  };
  typedef std::function<void(const struct mgos_config_lb &cfg,
                             bool cancel_previous)>
      UpdateFn;

  LightBulbControllerBase(struct mgos_config_lb *cfg, UpdateFn ud);
  LightBulbControllerBase(const LightBulbControllerBase &other) = delete;
  virtual ~LightBulbControllerBase();

  void UpdateOutput(struct mgos_config_lb *cfg, bool cancel_previous) const;

  virtual BulbType Type() = 0;

  bool IsOn() const;
  bool IsOff() const;

 protected:
  struct mgos_config_lb *cfg_;
  const UpdateFn update_;
};

template <class T>
struct Transition {
  T state_end;
  int64_t transition_time_micros = 0;
};

template <class T>
class LightBulbController : public LightBulbControllerBase {
 public:
  LightBulbController(struct mgos_config_lb *cfg)
      : LightBulbControllerBase(
            cfg, std::bind(&LightBulbController<T>::UpdateOutputSpecialized,
                           this, _1, _2)),
        transition_timer_(
            std::bind(&LightBulbController<T>::TransitionTimerCB, this)) {
  }
  LightBulbController(const LightBulbControllerBase &other) = delete;

 private:
  mgos::Timer transition_timer_;
  int64_t transition_start_ = 0;

  T state_start_{};
  T state_now_{};

  std::deque<Transition<T>> transitions_;

  virtual T ConfigToState(const struct mgos_config_lb &cfg) const = 0;
  virtual void ReportTransition(const T &next, const T &prev) = 0;
  virtual void UpdatePWM(const T &state) = 0;

  void StartPendingTransitions();
  void TransitionTimerCB();
  void UpdateOutputSpecialized(const struct mgos_config_lb &cfg,
                               bool cancel_previous);
};
}  // namespace shelly
