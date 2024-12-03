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

#include "shelly_hap_garage_door_opener.hpp"
#include "shelly_hap_input.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_sys_led_btn.hpp"
#include "shelly_temp_sensor_ntc.hpp"

namespace shelly {

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms UNUSED_ARG,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 5, 1));

  auto *in = new InputPin(1, 10, 1, MGOS_GPIO_PULL_NONE, true);
  in->AddHandler(std::bind(&HandleInputResetSequence, in, LED_GPIO, _1, _2));
  in->Init();
  inputs->emplace_back(in);

// not yet compatible
#ifdef MGOS_HAVE_ADC
  sys_temp->reset(new TempSensorSDNT1608X103F3950(3, 3.3f, 10000.0f));
#endif

  // std::unique_ptr<PowerMeter> pm()
  // BL0942 GPIO6 TX GPIO7 RX
  // const Status &st = pm->Init();
  // if (st.ok()) {
  //   pms->emplace_back(std::move(pm));
  // } else {
  //   const std::string &s = st.ToString();
  //   LOG(LL_ERROR, ("PM init failed: %s", s.c_str()));
  // }

  InitSysLED(LED_GPIO, LED_ON);
  InitSysBtn(BTN_GPIO, BTN_DOWN);
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  bool gdo_mode = mgos_sys_config_get_shelly_mode() == (int) Mode::kGarageDoor;
  if (gdo_mode) {
    hap::CreateHAPGDO(1, FindInput(1), FindInput(2), FindOutput(1),
                      FindOutput(1), mgos_sys_config_get_gdo1(), comps, accs,
                      svr, true);
  } else {
    CreateHAPSwitch(
        1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(), comps, accs,
        svr, mgos_sys_config_get_sw1_in_mode() != (int) InMode::kDetached);
  }
}

}  // namespace shelly
