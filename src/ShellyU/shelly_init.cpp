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

#include <cmath>

#include "mgos_hap.h"
#include "mgos_rpc.h"
#include "mgos_sys_config.h"

#include "shelly_hap_input.hpp"
#include "shelly_hap_temperature_sensor.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_mock.hpp"
#include "shelly_output.hpp"
#include "shelly_statusled.hpp"

namespace shelly {

static std::vector<std::unique_ptr<TempSensor>> sensors;

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  auto *in1 = new InputPin(1, 12, 1, MGOS_GPIO_PULL_NONE, true);
  in1->Init();
  inputs->emplace_back(in1);

  auto *in2 = new InputPin(2, 3, 0, MGOS_GPIO_PULL_NONE, false);
  in2->Init();
  inputs->emplace_back(in2);

  outputs->emplace_back(new OutputPin(1, 34, 1));

  outputs->emplace_back(new StatusLED(2, 2, 2, MGOS_NEOPIXEL_ORDER_GRB, nullptr,
                                      mgos_sys_config_get_led()));

  g_mock_sys_temp_sensor = new MockTempSensor(33);
  sys_temp->reset(g_mock_sys_temp_sensor);

  MockRPCInit();
  (void) pms;
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                  comps, accs, svr, false /* to_pri_acc */,
                  nullptr /* led_out */);

  for (int i = 0; i < 2; i++) {
    std::unique_ptr<TempSensor> temp(new MockTempSensor(25.125 + i));
    sensors.push_back(std::move(temp));
  }

  bool ext_switch_detected = false;  // can be set for testing purposes

  if (ext_switch_detected) {
    hap::CreateHAPInput(2, mgos_sys_config_get_in2(), comps, accs, svr);
  } else {
    if (!sensors.empty()) {
      CreateHAPSensors(&sensors, comps, accs, svr);
    }
  }

  comps->emplace_back(new StatusLEDComponent((StatusLED *) (FindOutput(2))));
}

}  // namespace shelly
