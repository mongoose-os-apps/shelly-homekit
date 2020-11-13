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

#include <memory>
#include <vector>

#include "mgos_sys_config.h"
#include "mgos_timers.h"

#include "shelly_common.hpp"
#include "shelly_component.hpp"
#include "shelly_hap_chars.hpp"
#include "shelly_hap_service.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"
#include "shelly_pm.hpp"
#include "shelly_switch.hpp"

namespace shelly {
namespace hap {

class Switch : public ShellySwitch {
 public:
  Switch(int id, Input *in, Output *out, PowerMeter *out_pm, Output *led_out,
         struct mgos_config_sw *cfg);
  virtual ~Switch();

  Status Init();
};

}  // namespace hap
}  // namespace shelly
