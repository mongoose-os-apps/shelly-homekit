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

#include "shelly_common.h"

namespace shelly {

class Input {
 public:
  enum class Event {
    kChange = 0,
    // TODO:
    // SINGLE = 1,
    // DOUBLE = 2,
    kReset = 4,
  };
  Input();
  virtual ~Input();

  virtual bool GetState() = 0;

  typedef int HandlerID;
  static constexpr HandlerID kInvalidHandlerID = -1;
  typedef std::function<void(Event ev, bool state)> HandlerFn;
  HandlerID AddHandler(HandlerFn h);
  void RemoveHandler(HandlerID hi);

 protected:
  void CallHandlers(Event ev, bool state);

 private:
  std::vector<HandlerFn> handlers_;

  Input(const Input &other) = delete;
};

class InputPin : public Input {
 public:
  InputPin(int id, int pin, int on_value, enum mgos_gpio_pull_type pull,
           bool enable_reset);
  virtual ~InputPin();

  int id() const;

  // Input interface impl.
  bool GetState() override;

 private:
  static void GPIOIntHandler(int pin, void *arg);

  void HandleGPIOInt();

  const int id_;
  const int pin_;
  const int on_value_;
  const bool enable_reset_;

  int change_cnt_;         // State change counter for reset.
  double last_change_ts_;  // Timestamp of last change (uptime).

  InputPin(const InputPin &other) = delete;
};

}  // namespace shelly
