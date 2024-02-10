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

#include "shelly_dht_sensor.hpp"
#include "shelly_hap_input.hpp"
#include "shelly_hap_stateless_switch.hpp"
#include "shelly_main.hpp"
#include "shelly_noisy_input_pin.hpp"
#include "shelly_sys_led_btn.hpp"
#include "shelly_temp_sensor_ntc.hpp"
#include "shelly_temp_sensor_ow.hpp"

namespace shelly {

static std::unique_ptr<Onewire> s_onewire;
static std::vector<std::unique_ptr<TempSensor>> sensors;

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs UNUSED_ARG,
                       std::vector<std::unique_ptr<PowerMeter>> *pms UNUSED_ARG,
                       std::unique_ptr<TempSensor> *sys_temp) {
  auto *in1 = new InputPin(1, 12, 1, MGOS_GPIO_PULL_NONE, true);
  in1->AddHandler(std::bind(&HandleInputResetSequence, in1, LED_GPIO, _1, _2));
  in1->Init();
  inputs->emplace_back(in1);

  auto *in2 = new NoisyInputPin(2, 14, 1, MGOS_GPIO_PULL_NONE, false);
  in2->Init();
  inputs->emplace_back(in2);

  auto *in3 = new NoisyInputPin(3, 27, 1, MGOS_GPIO_PULL_NONE, false);
  in3->Init();
  inputs->emplace_back(in3);

  auto *in4 = new NoisyInputPin(4, 26, 1, MGOS_GPIO_PULL_NONE, false);
  in4->Init();
  inputs->emplace_back(in4);

  sys_temp->reset(new TempSensorSDNT1608X103F3950(32, 3.3f, 10000.0f));

  int pin_out = 0;
  int pin_in = 1;

  if (DetectAddon(pin_in, pin_out)) {
    s_onewire.reset(new Onewire(pin_in, pin_out));
    sensors = s_onewire->DiscoverAll();
    if (sensors.empty()) {
      s_onewire.reset();
      sensors = DiscoverDHTSensors(pin_in, pin_out);
    }
  } else {
    InitSysLED(LED_GPIO, LED_ON);
  }
  InitSysBtn(BTN_GPIO, BTN_DOWN);
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  hap::CreateHAPInput(1, mgos_sys_config_get_in1(), comps, accs, svr);
  hap::CreateHAPInput(2, mgos_sys_config_get_in2(), comps, accs, svr);
  hap::CreateHAPInput(3, mgos_sys_config_get_in3(), comps, accs, svr);
  hap::CreateHAPInput(4, mgos_sys_config_get_in4(), comps, accs, svr);

  if (!sensors.empty()) {
    CreateHAPSensors(&sensors, comps, accs, svr);
  }
}

}  // namespace shelly
