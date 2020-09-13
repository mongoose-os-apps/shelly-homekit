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

#include "shelly_switch.h"

namespace shelly {

class ShellyHAPOutlet : public ShellySwitch {
 public:
  ShellyHAPOutlet(int id, Input *in, Output *out, PowerMeter *out_pm,
                  struct mgos_config_sw *cfg, HAPAccessoryServerRef *server,
                  const HAPAccessory *accessory);
  virtual ~ShellyHAPOutlet();

  Status Init();

 private:
  HAPError HandleOnRead(HAPAccessoryServerRef *server,
                        const HAPBoolCharacteristicReadRequest *request,
                        bool *value);
  HAPError HandleOnWrite(HAPAccessoryServerRef *server,
                         const HAPBoolCharacteristicWriteRequest *request,
                         bool value);
  HAPError HandleInUseRead(HAPAccessoryServerRef *server,
                           const HAPBoolCharacteristicReadRequest *request,
                           bool *value);
};

}  // namespace shelly
