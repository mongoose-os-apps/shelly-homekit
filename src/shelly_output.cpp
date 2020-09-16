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

#include "shelly_output.hpp"

#include "mgos.h"
#include "mgos_gpio.h"

namespace shelly {

Output::Output(int id) : id_(id) {
}

Output::~Output() {
}

int Output::id() const {
  return id_;
}

OutputPin::OutputPin(int id, int pin, int on_value)
    : Output(id), pin_(pin), on_value_(on_value) {
  mgos_gpio_set_mode(pin_, MGOS_GPIO_MODE_OUTPUT);
}

OutputPin::~OutputPin() {
}

bool OutputPin::GetState() {
  return (mgos_gpio_read_out(pin_) == on_value_);
}

Status OutputPin::SetState(bool on, const char *source) {
  bool cur_state = GetState();
  mgos_gpio_write(pin_, (on ? on_value_ : !on_value_));
  if (on == cur_state) return Status::OK();
  if (source == nullptr) source = "";
  LOG(LL_INFO,
      ("Output %d: %s -> %s (%s)", id(), OnOff(cur_state), OnOff(on), source));
  return Status::OK();
}

}  // namespace shelly
