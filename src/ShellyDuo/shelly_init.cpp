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

#include "shelly_cct_controller.hpp"
#include "shelly_hap_input.hpp"
#include "shelly_hap_light_bulb.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_light_bulb_controller.hpp"
#include "shelly_main.hpp"

namespace shelly {

void CreatePeripherals(UNUSED_ARG std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       UNUSED_ARG std::vector<std::unique_ptr<PowerMeter>> *pms,
                       UNUSED_ARG std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 5, 1));
  outputs->emplace_back(new OutputPin(2, 4, 1));
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr UNUSED_ARG) {
  std::unique_ptr<LightBulbControllerBase> lightbulb_controller;
  std::unique_ptr<hap::LightBulb> hap_light;
  auto *lb_cfg = (struct mgos_config_lb *) mgos_sys_config_get_lb1();

  lightbulb_controller.reset(
      new CCTController(lb_cfg, FindOutput(1), FindOutput(2)));

  hap_light.reset(new hap::LightBulb(
      1, nullptr, std::move(lightbulb_controller), lb_cfg, false));

  if (hap_light == nullptr || !hap_light->Init().ok()) {
    return;
  }

  // Use adaptive lightning when possible (CCT)
  std::unique_ptr<hap::AdaptiveLighting> adaptive_light;
  adaptive_light.reset(new hap::AdaptiveLighting(hap_light.get(), lb_cfg));
  auto st = adaptive_light->Init();
  if (st.ok()) {
    hap_light->SetAdaptiveLight(std::move(adaptive_light));
  }

  mgos::hap::Accessory *pri_acc = accs->front().get();
  shelly::hap::LightBulb *light_ref = hap_light.get();
  hap_light->set_primary(true);
  pri_acc->SetCategory(kHAPAccessoryCategory_Lighting);
  pri_acc->AddService(hap_light.get());
  pri_acc->SetIdentifyCB(
      [light_ref](const HAPAccessoryIdentifyRequest *request UNUSED_ARG) {
        light_ref->Identify();
        return kHAPError_None;
      });

  comps->push_back(std::move(hap_light));
}
}  // namespace shelly
