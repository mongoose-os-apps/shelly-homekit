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

#include "shelly_common.h"
#include "shelly_component.h"
#include "shelly_hap.h"
#include "shelly_hap_chars.h"
#include "shelly_input.h"
#include "shelly_output.h"
#include "shelly_pm.h"
#include "shelly_switch.h"

namespace shelly {
namespace hap {

class Switch : public ShellySwitch {
 public:
  Switch(int id, Input *in, Output *out, PowerMeter *out_pm,
         struct mgos_config_sw *cfg, HAPAccessoryServerRef *server,
         const HAPAccessory *accessory);
  virtual ~Switch();

  Status Init();

 private:
  HAPError HandleOnRead(HAPAccessoryServerRef *server,
                        const HAPBoolCharacteristicReadRequest *request,
                        bool *value);
  HAPError HandleOnWrite(HAPAccessoryServerRef *server,
                         const HAPBoolCharacteristicWriteRequest *request,
                         bool value);
};

}  // namespace hap
}  // namespace shelly
