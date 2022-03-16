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

#include "mgos_hap.hpp"
#include "mgos_sys_config.h"

#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_pm.hpp"
#include "shelly_sys_led_btn.hpp"

namespace shelly {

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp UNUSED_ARG) {
  outputs->emplace_back(new OutputPin(1, 32, 1));
  auto *in = new InputPin(1, 34, 0, MGOS_GPIO_PULL_UP, true);
  in->AddHandler(std::bind(&HandleInputResetSequence, in, 32, _1, _2));
  in->Init();
  inputs->emplace_back(in);

  InitSysLED(LED_GPIO, LED_ON);
  InitSysBtn(BTN_GPIO, BTN_DOWN);
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                  comps, accs, svr, true /* to_pri_acc */);
}

}  // namespace shelly
