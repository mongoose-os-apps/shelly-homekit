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

#include "mgos_ade7953.h"

#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_pm.hpp"
#include "shelly_pm_ade7953.hpp"
#include "shelly_sys_led_btn.hpp"
#include "shelly_temp_sensor_ntc.hpp"

#include <algorithm>

#include "shelly_hap_garage_door_opener.hpp"
#include "shelly_hap_input.hpp"
#include "shelly_hap_window_covering.hpp"
#include "shelly_main.hpp"

namespace shelly {

static struct mgos_ade7953 *s_ade7953 = NULL;

static Status PowerMeterInit(std::vector<std::unique_ptr<PowerMeter>> *pms) {
  const struct mgos_config_ade7953 ade7953_cfg = {
      .voltage_scale = .0000382602,
      .voltage_offset = -0.068,
      .current_scale_0 = 0.00000949523,
      .current_scale_1 = 0.00000949523,
      .current_offset_0 = -0.017,
      .current_offset_1 = -0.017,
      .apower_scale_0 = (1 / 164.0),
      .apower_scale_1 = (1 / 164.0),
      .aenergy_scale_0 = (1 / 25240.0),
      .aenergy_scale_1 = (1 / 25240.0),
      .voltage_pga_gain = 0,
      .current_pga_gain_0 = 0,
      .current_pga_gain_1 = 0,
  };

  int reset_pin = 33;
  mgos_gpio_set_mode(reset_pin, MGOS_GPIO_MODE_OUTPUT);
  mgos_gpio_write(reset_pin, 0);
  mgos_msleep(100);
  mgos_gpio_write(reset_pin, 1);
  mgos_gpio_set_mode(reset_pin, MGOS_GPIO_MODE_INPUT);


  s_ade7953 = mgos_ade7953_create(mgos_i2c_get_global(), &ade7953_cfg);

  if (s_ade7953 == nullptr) {
    LOG(LL_INFO, ("Failed to init ADE7953"));
    return mgos::Errorf(STATUS_UNAVAILABLE, "Failed to init ADE7953");
  }

  Status st;
  std::unique_ptr<PowerMeter> pm1(new ADE7953PowerMeter(1, s_ade7953, 1));
  if (!(st = pm1->Init()).ok()) return st;
  std::unique_ptr<PowerMeter> pm2(new ADE7953PowerMeter(2, s_ade7953, 0));
  if (!(st = pm2->Init()).ok()) return st;

  pms->emplace_back(std::move(pm1));
  pms->emplace_back(std::move(pm2));

  return Status::OK();
}

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 12, 1));
  outputs->emplace_back(new OutputPin(2, 13, 1));
  auto *in1 = new InputPin(1, 5, 1, MGOS_GPIO_PULL_NONE, true);
  in1->AddHandler(std::bind(&HandleInputResetSequence, in1, 4, _1, _2));
  in1->Init();
  inputs->emplace_back(in1);
  auto *in2 = new InputPin(2, 18, 1, MGOS_GPIO_PULL_NONE, false);
  in2->Init();
  inputs->emplace_back(in2);

  const Status &st = PowerMeterInit(pms);
  if (!st.ok()) {
    const std::string &s = st.ToString();
    LOG(LL_INFO, ("Failed to init ADE7953: %s", s.c_str()));
  }

  sys_temp->reset(new TempSensorSDNT1608X103F3950(35, 3.3f, 10000.0f));
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  // Roller-shutter mode.
  if (mgos_sys_config_get_shelly_mode() == 1) {
    const int id = 1;
    auto *wc_cfg = (struct mgos_config_wc *) mgos_sys_config_get_wc1();
    auto im = static_cast<hap::WindowCovering::InMode>(wc_cfg->in_mode);
    Input *in1 = FindInput(1), *in2 = FindInput(2);
    std::unique_ptr<hap::WindowCovering> wc(
        new hap::WindowCovering(id, in1, in2, FindOutput(1), FindOutput(2),
                                FindPM(1), FindPM(2), wc_cfg));
    if (wc == nullptr || !wc->Init().ok()) {
      return;
    }
    wc->set_primary(true);
    switch (im) {
      case hap::WindowCovering::InMode::kSeparateMomentary:
      case hap::WindowCovering::InMode::kSeparateToggle: {
        // Single accessory with a single primary service.
        mgos::hap::Accessory *pri_acc = (*accs)[0].get();
        pri_acc->SetCategory(kHAPAccessoryCategory_WindowCoverings);
        pri_acc->AddService(wc.get());
        break;
      }
      case hap::WindowCovering::InMode::kSingle:
      case hap::WindowCovering::InMode::kDetached: {
        std::unique_ptr<mgos::hap::Accessory> acc(
            new mgos::hap::Accessory(SHELLY_HAP_AID_BASE_WINDOW_COVERING + id,
                                     kHAPAccessoryCategory_BridgedAccessory,
                                     wc_cfg->name, GetIdentifyCB(), svr));
        acc->AddHAPService(&mgos_hap_accessory_information_service);
        acc->AddService(wc.get());
        accs->push_back(std::move(acc));
        if (im == hap::WindowCovering::InMode::kDetached) {
          hap::CreateHAPInput(1, mgos_sys_config_get_in1(), comps, accs, svr);
          hap::CreateHAPInput(2, mgos_sys_config_get_in2(), comps, accs, svr);
        } else if (wc_cfg->swap_inputs) {
          hap::CreateHAPInput(1, mgos_sys_config_get_in1(), comps, accs, svr);
        } else {
          hap::CreateHAPInput(2, mgos_sys_config_get_in2(), comps, accs, svr);
        }
        break;
      }
    }
    comps->emplace(comps->begin(), std::move(wc));
    return;
  }
  // Garage door opener mode.
  if (mgos_sys_config_get_shelly_mode() == 2) {
    auto *gdo_cfg = (struct mgos_config_gdo *) mgos_sys_config_get_gdo1();
    std::unique_ptr<hap::GarageDoorOpener> gdo(new hap::GarageDoorOpener(
        1, FindInput(1), FindInput(2), FindOutput(1), FindOutput(2), gdo_cfg));
    if (gdo == nullptr || !gdo->Init().ok()) {
      return;
    }
    gdo->set_primary(true);
    mgos::hap::Accessory *pri_acc = (*accs)[0].get();
    pri_acc->SetCategory(kHAPAccessoryCategory_GarageDoorOpeners);
    pri_acc->AddService(gdo.get());
    comps->emplace_back(std::move(gdo));
    return;
  }

  CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                  comps, accs, svr, false /* to_pri_acc */);
  CreateHAPSwitch(2, mgos_sys_config_get_sw2(), mgos_sys_config_get_in2(),
                  comps, accs, svr, false /* to_pri_acc */);
}

}  // namespace shelly
