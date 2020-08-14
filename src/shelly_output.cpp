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

#include "shelly_output.h"

#include "mgos.h"
#include "mgos_gpio.h"

namespace shelly {

OutputPin::OutputPin(int id, int pin, bool on_value, bool initial_state)
    : id_(id), pin_(pin), on_value_(on_value) {
  mgos_gpio_setup_output(pin_, (initial_state ? on_value_ : !on_value_));
}

OutputPin::~OutputPin() {
}

bool OutputPin::GetState() {
  return (mgos_gpio_read_out(pin_) == on_value_);
}

Status OutputPin::SetState(bool on) {
  bool cur_state = GetState();
  if (on == cur_state) return Status::OK();
  LOG(LL_INFO, ("Output %d: %s -> %s", id_, OnOff(cur_state), OnOff(on)));
  mgos_gpio_write(pin_, (on ? on_value_ : !on_value_));
  return Status::OK();
}

}  // namespace shelly
