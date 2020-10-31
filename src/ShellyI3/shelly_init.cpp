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

#include "shelly_hap_stateless_switch.hpp"
#include "shelly_main.hpp"
#include "shelly_noisy_input_pin.hpp"
#include "shelly_temp_sensor_ntc.hpp"

namespace shelly {

// i3 inputs are super noisy and cause interrupt storm.
// Instead we run a frequent hardware timer that smoothes things out.
void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  auto *in1 = new NoisyInputPin(1, 14, 1, MGOS_GPIO_PULL_DOWN, true);
  in1->AddHandler(std::bind(&HandleInputResetSequence, in1, -1, _1, _2));
  inputs->emplace_back(in1);
  in1->Init();

  auto *in2 = new NoisyInputPin(2, 12, 1, MGOS_GPIO_PULL_DOWN, false);
  inputs->emplace_back(in2);
  in2->Init();

  auto *in3 = new NoisyInputPin(3, 13, 1, MGOS_GPIO_PULL_DOWN, false);
  inputs->emplace_back(in3);
  in3->Init();

  sys_temp->reset(new TempSensorSDNT1608X103F3450(0, 3.3f, 33000.0f));

  (void) pms;
}

void CreateComponents(std::vector<Component *> *comps,
                      std::vector<std::unique_ptr<hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  CreateHAPStatelessSwitch(1, mgos_sys_config_get_ssw1(), comps, accs, svr);
  CreateHAPStatelessSwitch(2, mgos_sys_config_get_ssw2(), comps, accs, svr);
  CreateHAPStatelessSwitch(3, mgos_sys_config_get_ssw3(), comps, accs, svr);
}

}  // namespace shelly
