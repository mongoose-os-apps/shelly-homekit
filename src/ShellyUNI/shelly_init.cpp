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

#include <algorithm>

#include "shelly_dht_sensor.hpp"
#include "shelly_hap_garage_door_opener.hpp"
#include "shelly_hap_input.hpp"
#include "shelly_hap_window_covering.hpp"
#include "shelly_main.hpp"
#include "shelly_noisy_input_pin.hpp"
#include "shelly_sys_led_btn.hpp"
#include "shelly_temp_sensor_ow.hpp"

namespace shelly {

static std::unique_ptr<Onewire> s_onewire;
static std::vector<std::unique_ptr<TempSensor>> sensors;

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, RELAY1_GPIO, 1));
  outputs->emplace_back(new OutputPin(2, RELAY2_GPIO, 1));
#ifdef SWITCH_NOISY
  auto *in1 = new NoisyInputPin(1, SWITCH1_GPIO, 1, MGOS_GPIO_PULL_NONE, true);
  auto *in2 = new NoisyInputPin(2, SWITCH2_GPIO, 1, MGOS_GPIO_PULL_NONE, false);
#else
  auto *in1 = new InputPin(1, SWITCH1_GPIO, 1, MGOS_GPIO_PULL_NONE, true);
  auto *in2 = new InputPin(2, SWITCH2_GPIO, 1, MGOS_GPIO_PULL_NONE, false);
#endif
  in1->AddHandler(std::bind(&HandleInputResetSequence, in1, LED_GPIO, _1, _2));
  in1->Init();
  inputs->emplace_back(in1);

  in2->Init();
  inputs->emplace_back(in2);

  s_onewire.reset(new Onewire(SENSOR_GPIO, SENSOR_GPIO));
  sensors = s_onewire->DiscoverAll();
  if (sensors.empty()) {
    s_onewire.reset();
    sensors = DiscoverDHTSensors(SENSOR_GPIO, SENSOR_GPIO);
  }

  InitSysLED(LED_GPIO, LED_ON);
  InitSysBtn(BTN_GPIO, BTN_DOWN);
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  bool gdo_mode = mgos_sys_config_get_shelly_mode() == (int) Mode::kGarageDoor;

  if (gdo_mode) {
    hap::CreateHAPGDO(1, FindInput(1), FindInput(2), FindOutput(1),
                      FindOutput(2), mgos_sys_config_get_gdo1(), comps, accs,
                      svr, true);
  } else {
    CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                    comps, accs, svr, false /* to_pri_acc */);
    CreateHAPSwitch(2, mgos_sys_config_get_sw2(), mgos_sys_config_get_in2(),
                    comps, accs, svr, false /* to_pri_acc */);
  }

  if (!sensors.empty()) {
    CreateHAPSensors(&sensors, comps, accs, svr);
  }
}

}  // namespace shelly
