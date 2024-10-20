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
#include "shelly_hap_adaptive_lighting.hpp"
#include "shelly_hap_input.hpp"
#include "shelly_hap_light_bulb.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_rgbw_controller.hpp"
#include "shelly_sys_led_btn.hpp"
#include "shelly_white_controller.hpp"

namespace shelly {

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms UNUSED_ARG,
                       std::unique_ptr<TempSensor> *sys_temp UNUSED_ARG) {
  outputs->emplace_back(new OutputPin(1, GPIO_R, 1));  // CW0
  outputs->emplace_back(new OutputPin(2, GPIO_G, 1));  // WW0
  outputs->emplace_back(new OutputPin(3, GPIO_B, 1));  // CW1
  outputs->emplace_back(new OutputPin(4, GPIO_W, 1));  // WW1
  auto *in = new InputPin(1, GPIO_I1, 1, MGOS_GPIO_PULL_NONE, true);
  in->AddHandler(std::bind(&HandleInputResetSequence, in, 0, _1, _2));
  in->Init();
  inputs->emplace_back(in);

#ifdef GPIO_I2
  in = new InputPin(2, GPIO_I2, 1, MGOS_GPIO_PULL_NONE, true);
  in->Init();
  inputs->emplace_back(in);

  in = new InputPin(3, GPIO_I3, 1, MGOS_GPIO_PULL_NONE, true);
  in->Init();
  inputs->emplace_back(in);

  in = new InputPin(4, GPIO_I4, 1, MGOS_GPIO_PULL_NONE, true);
  in->Init();
  inputs->emplace_back(in);
#endif

  InitSysLED(LED_GPIO, LED_ON);
  InitSysBtn(BTN_GPIO, BTN_DOWN);
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  std::unique_ptr<LightBulbControllerBase> lightbulb_controller;
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
  } else if (mode == (int) Mode::kDefault) {
#if defined ( MGOS_CONFIG_HAVE_SW1 ) && defined ( MGOS_CONFIG_HAVE_IN1 )
    CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                    comps, accs, svr, false /* to_pri_acc */);
#endif
#if defined ( MGOS_CONFIG_HAVE_SW2 ) && defined ( MGOS_CONFIG_HAVE_IN2 )
    CreateHAPSwitch(2, mgos_sys_config_get_sw2(), mgos_sys_config_get_in2(),
                    comps, accs, svr, false /* to_pri_acc */);
#endif
#if defined ( MGOS_CONFIG_HAVE_SW3 ) && defined ( MGOS_CONFIG_HAVE_IN3 )
    CreateHAPSwitch(3, mgos_sys_config_get_sw3(), mgos_sys_config_get_in3(),
                    comps, accs, svr, false /* to_pri_acc */);
#endif
#if defined ( MGOS_CONFIG_HAVE_SW4 ) && defined ( MGOS_CONFIG_HAVE_IN4 )
    CreateHAPSwitch(4, mgos_sys_config_get_sw4(), mgos_sys_config_get_in4(),
                    comps, accs, svr, false /* to_pri_acc */);
#endif
    return;
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
          new WhiteController(lb_cfg, FindOutput(out_pin++)));
      is_optional = (mgos_sys_config_get_shelly_mode() != (int) Mode::kRGBpW);
    } else {  // Mode::kRGBpW
      lightbulb_controller.reset(new RGBWController(
          lb_cfg, FindOutput(1), FindOutput(2), FindOutput(3), nullptr));
      mode = (int) Mode::kWhite;  // last bulb is White
      out_pin += 3;
    }

    Input *in = FindInput(i + 1);

    hap_light.reset(new hap::LightBulb(
        i + 1, in, std::move(lightbulb_controller), lb_cfg, is_optional));

    if (hap_light == nullptr) return;
    auto st = hap_light->Init();
    if (!st.ok()) {
      LOG(LL_ERROR, ("LightBulb init failed: %s", st.ToString().c_str()));
      return;
    }

    // Use adaptive lightning when possible (CCT)
    std::unique_ptr<hap::AdaptiveLighting> adaptive_light;
    adaptive_light.reset(new hap::AdaptiveLighting(hap_light.get(), lb_cfg));
    st = adaptive_light->Init();
    if (st.ok()) {
      hap_light->SetAdaptiveLight(std::move(adaptive_light));
    }

    bool to_pri_acc = (ndev == 1);  // only device will become primary accessory
                                    // regardless of sw_hidden status
    bool sw_hidden = is_optional && lb_cfg->svc_hidden;

    mgos::hap::Accessory *pri_acc = accs->front().get();
    shelly::hap::LightBulb *light_ref = hap_light.get();
    if (to_pri_acc) {
      hap_light->set_primary(true);
      pri_acc->SetCategory(kHAPAccessoryCategory_Lighting);
      pri_acc->AddService(light_ref);
      pri_acc->SetIdentifyCB(
          [light_ref](const HAPAccessoryIdentifyRequest *request UNUSED_ARG) {
            light_ref->Identify();
            return kHAPError_None;
          });
    } else if (!sw_hidden) {
      int aid = SHELLY_HAP_AID_BASE_LIGHTING + i;

      std::unique_ptr<mgos::hap::Accessory> acc(new mgos::hap::Accessory(
          aid, kHAPAccessoryCategory_BridgedAccessory, lb_cfg->name,
          [light_ref](const HAPAccessoryIdentifyRequest *request UNUSED_ARG) {
            light_ref->Identify();
            return kHAPError_None;
          },
          svr));
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
