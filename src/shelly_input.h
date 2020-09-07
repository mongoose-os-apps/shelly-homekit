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

#include "shelly_common.h"

#include "mgos_gpio.h"

namespace shelly {

class Input {
 public:
  enum class Event {
    CHANGE = 0,
    // TODO:
    // SINGLE = 1,
    // DOUBLE = 2,
  };
  virtual StatusOr<bool> GetState() = 0;

  typedef std::function<void(Event ev, bool state)> HandlerFn;
  virtual void SetHandler(HandlerFn h) = 0;
};

class InputPin : public Input {
 public:
  InputPin(int id, int pin, bool on_value, enum mgos_gpio_pull_type pull);
  virtual ~InputPin();

  // Input interface impl.
  StatusOr<bool> GetState() override;
  virtual void SetHandler(HandlerFn h) override;

 private:
  static void GPIOIntHandler(int pin, void *arg);

  void HandleGPIOInt();

  const int id_;
  const int pin_;
  const bool on_value_;

  HandlerFn handler_;

  InputPin(const InputPin &other) = delete;
};

}  // namespace shelly
