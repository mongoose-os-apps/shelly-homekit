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

#include "mgos.hpp"

#pragma once

namespace shelly {

class LightBulbController {
 public:
  explicit LightBulbController(struct mgos_config_lb *cfg);
  LightBulbController(const LightBulbController &other) = delete;
  virtual ~LightBulbController();

  virtual void UpdateOutput() = 0;

  bool IsOn() const;
  bool IsOff() const;

 protected:
  struct mgos_config_lb *cfg_;
};

}  // namespace shelly
