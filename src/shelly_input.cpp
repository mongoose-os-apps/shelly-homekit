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

#include "shelly_input.h"

#include "mgos.h"
#include "mgos_gpio.h"

namespace shelly {

Input::Input() {
}

Input::~Input() {
}

Input::HandlerID Input::AddHandler(HandlerFn h) {
  int i;
  for (i = 0; i < (int) handlers_.size(); i++) {
    if (handlers_[i] == nullptr) {
      handlers_[i] = h;
      return i;
    }
  }
  handlers_.push_back(h);
  return i;
}

void Input::RemoveHandler(HandlerID hi) {
  if (hi < 0) return;
  handlers_[hi] = nullptr;
}

void Input::CallHandlers(Event ev, bool state) {
  for (auto &h : handlers_) {
    h(ev, state);
  }
}

InputPin::InputPin(int id, int pin, int on_value, enum mgos_gpio_pull_type pull,
                   bool enable_reset)
    : id_(id),
      pin_(pin),
      on_value_(on_value),
      enable_reset_(enable_reset),
      change_cnt_(0),
      last_change_ts_(0) {
  mgos_gpio_setup_input(pin_, pull);
  mgos_gpio_set_button_handler(pin_, pull, MGOS_GPIO_INT_EDGE_ANY, 20,
                               GPIOIntHandler, this);
}

InputPin::~InputPin() {
  mgos_gpio_remove_int_handler(pin_, nullptr, nullptr);
}

int InputPin::id() const {
  return id_;
}

bool InputPin::GetState() {
  return (mgos_gpio_read(pin_) == on_value_);
}

// static
void InputPin::GPIOIntHandler(int pin, void *arg) {
  static_cast<InputPin *>(arg)->HandleGPIOInt();
  (void) pin;
}

void InputPin::HandleGPIOInt() {
  bool cur_state = (mgos_gpio_read(pin_) == on_value_);
  LOG(LL_INFO, ("Input %d: %s", id_, OnOff(cur_state)));
  CallHandlers(Event::kChange, cur_state);
  double now = mgos_uptime();
  if (enable_reset_) {
    if (now - last_change_ts_ > 10) {
      change_cnt_ = 0;
    }
    change_cnt_++;
    if (change_cnt_ >= 10) {
      change_cnt_ = 0;
      CallHandlers(Event::kReset, cur_state);
    }
  }
  last_change_ts_ = now;
}

}  // namespace shelly
