/*
 * Copyright (c) 2020 Deomid "rojer" Ryabkov
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

#include "shelly_input.hpp"

#include "mgos_gpio.h"
#include "mgos_timers.hpp"

namespace shelly {

class InputPin : public Input {
 public:
  static constexpr int kDefaultShortPressDurationMs = 500;
  static constexpr int kDefaultLongPressDurationMs = 1000;

  struct Config {
    int pin;
    int on_value;
    enum mgos_gpio_pull_type pull;
    bool enable_reset;
    int short_press_duration_ms;
    int long_press_duration_ms;
  };

  InputPin(int id, int pin, int on_value, enum mgos_gpio_pull_type pull,
           bool enable_reset);
  InputPin(int id, const Config &cfg);
  virtual ~InputPin();

  // Input interface impl.
  bool GetState() override;
  virtual void Init() override;
  void SetInvert(bool invert) override;

 protected:
  virtual bool ReadPin();
  void HandleGPIOInt();

  const Config cfg_;
  bool invert_ = false;

 private:
  enum class State {
    kIdle = 0,
    kWaitOffSingle = 1,
    kWaitOnDouble = 2,
    kWaitOffDouble = 3,
    kWaitOffLong = 4,
  };

  static void GPIOIntHandler(int pin, void *arg);

  void DetectReset(double now, bool cur_state);

  void HandleTimer();

  bool last_state_ = false;
  int change_cnt_ = 0;         // State change counter for reset.
  double last_change_ts_ = 0;  // Timestamp of last change (uptime).

  State state_ = State::kIdle;
  int timer_cnt_ = 0;
  mgos::ScopedTimer timer_;

  InputPin(const InputPin &other) = delete;
};

}  // namespace shelly
