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

  if (mode == (int) Mode::kCCT) {
    ndev = 2;
  } else if (mode == (int) Mode::kWhite) {
    ndev = 4;
  } else if (mode == (int) Mode::kRGBpW) {
    ndev = 2;
  }

  int out_pin = 1;
  bool first_detatched_input = true;
  bool is_optional = false;

  for (int i = 0; i < ndev; i++) {
    lb_cfg = lb_cfgs[i];

    if (mode == (int) Mode::kRGB) {
      lightbulb_controller.reset(new RGBWController(
          lb_cfg, FindOutput(1), FindOutput(2), FindOutput(3), nullptr));
      FindOutput(4)->SetStatePWM(0.0f, "cc");
    } else if (mode == (int) Mode::kRGBW) {
      lightbulb_controller.reset(new RGBWController(
          lb_cfg, FindOutput(1), FindOutput(2), FindOutput(3), FindOutput(4)));
    } else if (mode == (int) Mode::kCCT) {
      lightbulb_controller.reset(new CCTController(lb_cfg, FindOutput(out_pin),
                                                   FindOutput(out_pin + 1)));
      out_pin += 2;
      is_optional = true;
    } else if (mode == (int) Mode::kWhite) {
      lightbulb_controller.reset(
          new LightController(lb_cfg, FindOutput(out_pin++)));
      is_optional = (mgos_sys_config_get_shelly_mode() != (int) Mode::kRGBpW);
    } else {  // Mode::kRGBpW
      lightbulb_controller.reset(new RGBWController(
          lb_cfg, FindOutput(1), FindOutput(2), FindOutput(3), nullptr));
      mode = (int) Mode::kWhite;  // last bulb is White
      out_pin += 3;
    }

    Input *in = FindInput(1);
    if (i != 0) {
      in = nullptr;  // support input only for first device
    }

    hap_light.reset(new hap::LightBulb(
        i + 1, in, std::move(lightbulb_controller), lb_cfg, is_optional));

    if (hap_light == nullptr) return;
    auto st = hap_light->Init();
    if (!st.ok()) {
      LOG(LL_ERROR, ("LightBulb init failed: %s", st.ToString().c_str()));
      return;
    }

    bool to_pri_acc = (ndev == 1);  // only device will become primary accessory
                                    // regardless of sw_hidden status
    bool sw_hidden = is_optional && lb_cfg->svc_hidden;

    mgos::hap::Accessory *pri_acc = accs->front().get();
    if (to_pri_acc) {
      hap_light->set_primary(true);
      pri_acc->SetCategory(kHAPAccessoryCategory_Lighting);
      pri_acc->AddService(hap_light.get());
    } else if (!sw_hidden) {
      int aid = SHELLY_HAP_AID_BASE_LIGHTING + i;

      std::unique_ptr<mgos::hap::Accessory> acc(
          new mgos::hap::Accessory(aid, kHAPAccessoryCategory_BridgedAccessory,
                                   lb_cfg->name, nullptr, svr));
      acc->AddHAPService(&mgos_hap_accessory_information_service);
      acc->AddService(hap_light.get());
      accs->push_back(std::move(acc));
    }
    comps->push_back(std::move(hap_light));

    if (lb_cfg->in_mode == (int) InMode::kDetached && first_detatched_input) {
      hap::CreateHAPInput(1, mgos_sys_config_get_in1(), comps, accs, svr);
      first_detatched_input = false;
    }
  }
}
}  // namespace shelly
