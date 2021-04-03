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

#include "mgos_sys_config.h"

#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_mock.hpp"
#include "shelly_output.hpp"

namespace shelly {

const std::set<std::string> g_compatibleFirmwareNames{"todo!!!"};

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  std::unique_ptr<Input> in1(new InputPin(1, 12, 1, MGOS_GPIO_PULL_NONE, true));
  in1->Init();
  inputs->emplace_back(std::move(in1));
  std::unique_ptr<Input> in2(new InputPin(2, 13, 1, MGOS_GPIO_PULL_NONE, true));
  in2->Init();
  inputs->emplace_back(std::move(in2));

  outputs->emplace_back(new OutputPin(1, 34, 1));
  outputs->emplace_back(new OutputPin(2, 35, 1));

  std::unique_ptr<MockPowerMeter> pm1(new MockPowerMeter(1));
  pm1->Init();
  g_mock_pms.push_back(pm1.get());
  pms->emplace_back(std::move(pm1));
  std::unique_ptr<MockPowerMeter> pm2(new MockPowerMeter(2));
  pm2->Init();
  g_mock_pms.push_back(pm2.get());
  pms->emplace_back(std::move(pm2));

  g_mock_sys_temp_sensor = new MockTempSensor(33);
  sys_temp->reset(g_mock_sys_temp_sensor);

  MockRPCInit();
}

}  // namespace shelly
