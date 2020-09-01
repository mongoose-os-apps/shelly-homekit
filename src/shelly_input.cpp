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

InputPin::InputPin(int id, int pin, bool on_value,
                   enum mgos_gpio_pull_type pull)
    : id_(id), pin_(pin), on_value_(on_value) {
  mgos_gpio_setup_input(pin_, pull);
  mgos_gpio_set_button_handler(pin_, pull, MGOS_GPIO_INT_EDGE_ANY, 20,
                               GPIOIntHandler, this);
}

InputPin::~InputPin() {
  mgos_gpio_remove_int_handler(pin_, nullptr, nullptr);
}

StatusOr<bool> InputPin::GetState() {
  return (mgos_gpio_read(pin_) == on_value_);
}

void InputPin::SetHandler(HandlerFn h) {
  handler_ = h;
}

// static
void InputPin::GPIOIntHandler(int pin, void *arg) {
  static_cast<InputPin *>(arg)->HandleGPIOInt();
  (void) pin;
}

void InputPin::HandleGPIOInt() {
  bool cur_state = (mgos_gpio_read(pin_) == on_value_);
  LOG(LL_INFO, ("Input %d: %s", id_, OnOff(cur_state)));
  if (handler_) handler_(InputEvent::CHANGE, cur_state);
}

}  // namespace shelly
