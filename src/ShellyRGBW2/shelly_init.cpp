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
#include "shelly_hap_rgb.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"

namespace shelly {

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 12, 1));  // R
  outputs->emplace_back(new OutputPin(2, 15, 1));  // G
  outputs->emplace_back(new OutputPin(3, 14, 1));  // B
  outputs->emplace_back(new OutputPin(4, 4, 1));   // W
  auto *in = new InputPin(1, 5, 1, MGOS_GPIO_PULL_NONE, true);
  in->AddHandler(std::bind(&HandleInputResetSequence, in, 0, _1, _2));
  in->Init();
  inputs->emplace_back(in);
  (void) sys_temp;
  (void) pms;
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  auto *rgb_cfg = (struct mgos_config_rgb *) mgos_sys_config_get_rgb1();
  std::unique_ptr<hap::RGB> rgb(new hap::RGB(
      1, FindInput(1), FindOutput(1), FindOutput(2), FindOutput(3), rgb_cfg));
  if (rgb == nullptr || !rgb->Init().ok()) {
    return;
  }
  rgb->set_primary(true);
  mgos::hap::Accessory *pri_acc = (*accs)[0].get();
  pri_acc->SetCategory(kHAPAccessoryCategory_Lighting);
  pri_acc->AddService(rgb.get());
  comps->emplace_back(std::move(rgb));

  if (rgb_cfg->in_mode == 3) {
    hap::CreateHAPInput(1, mgos_sys_config_get_in1(), comps, accs, svr);
  }
}

}  // namespace shelly
