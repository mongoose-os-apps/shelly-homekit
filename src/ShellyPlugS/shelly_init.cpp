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

#include "shelly_main.hpp"
#include "shelly_temperature_ntc.hpp"

namespace shelly {

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TemperatureSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 15, 1));
  auto *in = new InputPin(1, 13, 1, MGOS_GPIO_PULL_NONE, true);
  in->AddHandler(std::bind(&HandleInputResetSequence, in, 15, _1, _2));
  inputs->emplace_back(in);
  sys_temp->reset(new TemperatureSensorSDNT1608X103F3450(0, 3.3f, 33000.0f));
  (void) pms;
}

void CreateComponents(std::vector<Component *> *comps,
                      std::vector<std::unique_ptr<hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  // Single switch with non-detached input = only one accessory.
  bool to_pri_acc = (mgos_sys_config_get_sw1_in_mode() != 3);
  CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_ssw1(),
                  comps, accs, svr, to_pri_acc);
}

}  // namespace shelly
