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

#include <vector>

#include "mgos_event.h"
#include "mgos_gpio.h"
#include "mgos_timers.h"

#include "shelly_common.h"

namespace shelly {

class Input {
 public:
  enum class Event {
    kChange = 0,
    kSingle = 1,
    kDouble = 2,
    kLong = 3,
    kReset = 4,
  };
  explicit Input(int id);
  virtual ~Input();

  static const char *EventName(Event ev);

  int id() const;
  virtual bool GetState() = 0;

  typedef int HandlerID;
  static constexpr HandlerID kInvalidHandlerID = -1;
  typedef std::function<void(Event ev, bool state)> HandlerFn;
  HandlerID AddHandler(HandlerFn h);
  void RemoveHandler(HandlerID hi);

 protected:
  void CallHandlers(Event ev, bool state);

 private:
  const int id_;
  std::vector<HandlerFn> handlers_;

  Input(const Input &other) = delete;
};

class InputPin : public Input {
 public:
  InputPin(int id, int pin, int on_value, enum mgos_gpio_pull_type pull,
           bool enable_reset);
  virtual ~InputPin();

  // Input interface impl.
  bool GetState() override;

 private:
  static constexpr int kLongPressDurationMs = 1000;

  enum class State {
    kIdle = 0,
    kWaitOffSingle = 1,
    kWaitOnDouble = 2,
    kWaitOffDouble = 3,
    kWaitOffLong = 4,
  };

  static void GPIOIntHandler(int pin, void *arg);
  static void TimerCB(void *arg);

  void SetTimer(int ms);
  void ClearTimer();

  void DetectReset(double now, bool cur_state);

  void HandleGPIOInt();
  void HandleTimer();

  const int pin_;
  const int on_value_;
  const bool enable_reset_;

  int change_cnt_ = 0;         // State change counter for reset.
  double last_change_ts_ = 0;  // Timestamp of last change (uptime).

  State state_ = State::kIdle;
  int timer_cnt_ = 0;
  mgos_timer_id timer_id_ = MGOS_INVALID_TIMER_ID;

  InputPin(const InputPin &other) = delete;
};

}  // namespace shelly
