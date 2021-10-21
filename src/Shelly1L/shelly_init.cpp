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

#include "shelly_hap_input.hpp"
#include "shelly_input.hpp"
#include "shelly_main.hpp"
#include "shelly_noisy_input_pin.hpp"
#include "shelly_pm_bl0937.hpp"
#include "shelly_switch.hpp"
#include "shelly_temp_sensor_ntc.hpp"

namespace shelly {

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 5, 1));
  auto *in1 = new NoisyInputPin(1, 4, 1, MGOS_GPIO_PULL_NONE, true);
  in1->AddHandler(std::bind(&HandleInputResetSequence, in1, 5, _1, _2));
  in1->Init();
  inputs->emplace_back(in1);
  auto *in2 = new NoisyInputPin(2, 14, 1, MGOS_GPIO_PULL_NONE, false);
  in2->Init();
  inputs->emplace_back(in2);
#if 0  // 1L uses BL0937 in current-only mode. This is not yet supported.
  std::unique_ptr<PowerMeter> pm(new BL0937PowerMeter(1, -1, 4, -1));
  if (pm->Init().ok()) {
    pms->emplace_back(std::move(pm));
  }
#else
  (void) pms;
#endif
  sys_temp->reset(new TempSensorSDNT1608X103F3950(0, 3.3f, 33000.0f));
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                  comps, accs, svr, false);
  if (mgos_sys_config_get_sw1_in_mode() == (int) InMode::kEdgeBoth ||
      mgos_sys_config_get_sw1_in_mode() == (int) InMode::kActivation) {
    ShellySwitch *sw = reinterpret_cast<ShellySwitch *>(comps->back().get());
    sw->AddInput(FindInput(2));
  } else {
    hap::CreateHAPInput(2, mgos_sys_config_get_in2(), comps, accs, svr);
  }
}

}  // namespace shelly
