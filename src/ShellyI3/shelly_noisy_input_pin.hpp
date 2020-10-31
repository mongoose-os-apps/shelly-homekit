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

namespace shelly {

class NoisyInputPin : public InputPin {
 public:
  NoisyInputPin(int id, int pin, int on_value, enum mgos_gpio_pull_type pull,
                bool enable_reset);
  virtual ~NoisyInputPin();

  void Init() override;

  void Check();

 private:
  bool ReadPin() override;
};

}  // namespace shelly
