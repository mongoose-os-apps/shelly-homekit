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
#include "shelly_light_controller.hpp"
#include "shelly_main.hpp"
#include "shelly_rgbw_controller.hpp"

namespace shelly {

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 12, 1));  // R / CW0
  outputs->emplace_back(new OutputPin(2, 15, 1));  // G / WW0
  outputs->emplace_back(new OutputPin(3, 14, 1));  // B / CW1
  outputs->emplace_back(new OutputPin(4, 4, 1));   // W / WW1
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
  std::unique_ptr<LightBulbController> lightbulb_controller;
  std::unique_ptr<hap::LightBulb> hap_light;
  struct mgos_config_lb *lb_cfg;

  struct mgos_config_lb *lb_cfgs[4] = {
      (struct mgos_config_lb *) mgos_sys_config_get_lb1(),
      (struct mgos_config_lb *) mgos_sys_config_get_lb2(),
      (struct mgos_config_lb *) mgos_sys_config_get_lb3(),
      (struct mgos_config_lb *) mgos_sys_config_get_lb4(),
  };

  int mode = mgos_sys_config_get_shelly_mode();

  int ndev = 1;

  if (mode == 5) {
    ndev = 2;
  } else if (mode == 6) {
    ndev = 4;
  } else if (mode == 7) {
    ndev = 2;
  }

  int out_pin = 1;
  bool first_detatched_input = true;

  for (int i = 0; i < ndev; i++) {
    lb_cfg = lb_cfgs[i];

    if (mode == 3) {
      lightbulb_controller.reset(new RGBWController(
          lb_cfg, FindOutput(1), FindOutput(2), FindOutput(3), nullptr));
      FindOutput(4)->SetStatePWM(0.0f, "cc");
    } else if (mode == 4) {
      lightbulb_controller.reset(new RGBWController(
          lb_cfg, FindOutput(1), FindOutput(2), FindOutput(3), FindOutput(4)));
    } else if (mode == 5) {
      lightbulb_controller.reset(new CCTController(lb_cfg, FindOutput(out_pin),
                                                   FindOutput(out_pin + 1)));
      out_pin += 2;
    } else if (mode == 6) {
      lightbulb_controller.reset(
          new LightController(lb_cfg, FindOutput(out_pin++)));
    } else {  // mode 7 (RGB+W)
      lightbulb_controller.reset(new RGBWController(
          lb_cfg, FindOutput(1), FindOutput(2), FindOutput(3), nullptr));
      mode = 6;  // last bulb is w
      out_pin += 3;
    }

    hap_light.reset(new hap::LightBulb(
        i + 1, FindInput(1), std::move(lightbulb_controller), lb_cfg));

    if (hap_light == nullptr || !hap_light->Init().ok()) {
      return;
    }

    mgos::hap::Accessory *pri_acc = (*accs)[0].get();
    pri_acc->SetCategory(kHAPAccessoryCategory_Lighting);
    pri_acc->AddService(hap_light.get());
    comps->emplace_back(std::move(hap_light));

    if (lb_cfg->in_mode == 3 && first_detatched_input) {
      hap::CreateHAPInput(1, mgos_sys_config_get_in1(), comps, accs, svr);
      first_detatched_input = false;
    }
  }
}
}  // namespace shelly
