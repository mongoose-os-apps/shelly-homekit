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

#include "shelly_hap_garage_door_opener.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_pm_bl0937.hpp"
#include "shelly_temp_sensor_ntc.hpp"
#include "shelly_temp_sensor_ow.hpp"

#define MAX_TS_NUM 3

namespace shelly {

static std::unique_ptr<Onewire> s_onewire;

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 15, 1));
  auto *in = new InputPin(1, 4, 1, MGOS_GPIO_PULL_NONE, true);
  in->AddHandler(std::bind(&HandleInputResetSequence, in, 15, _1, _2));
  in->Init();
  inputs->emplace_back(in);

  std::unique_ptr<PowerMeter> pm(
      new BL0937PowerMeter(1, 5 /* CF */, -1 /* CF1 */, -1 /* SEL */, 2,
                           mgos_sys_config_get_bl0937_power_coeff()));
  const Status &st = pm->Init();
  if (st.ok()) {
    pms->emplace_back(std::move(pm));
  } else {
    const std::string &s = st.ToString();
    LOG(LL_ERROR, ("PM init failed: %s", s.c_str()));
  }
  sys_temp->reset(new TempSensorSDNT1608X103F3950(0, 3.3f, 33000.0f));
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  if (mgos_sys_config_get_shelly_mode() == 2) {
    // Garage door opener mode.
    auto *gdo_cfg = (struct mgos_config_gdo *) mgos_sys_config_get_gdo1();
    std::unique_ptr<hap::GarageDoorOpener> gdo(
        new hap::GarageDoorOpener(1, FindInput(1), nullptr /* in_open */,
                                  FindOutput(1), FindOutput(1), gdo_cfg));
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

  // Sensor Discovery
  std::unique_ptr<Onewire> ow(std::move(s_onewire));
  if (ow == nullptr) {
    ow.reset(new Onewire(3, 0));
  }
  auto sensors = ow->DiscoverAll();

  // Single switch with non-detached input and no sensors = only one accessory.
  bool to_pri_acc = (sensors.empty() && (mgos_sys_config_get_sw1_in_mode() !=
                                         (int) InMode::kDetached));
  CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                  comps, accs, svr, to_pri_acc);

  if (!sensors.empty()) {
    struct mgos_config_ts *ts_cfgs[MAX_TS_NUM] = {
        (struct mgos_config_ts *) mgos_sys_config_get_ts1(),
        (struct mgos_config_ts *) mgos_sys_config_get_ts2(),
        (struct mgos_config_ts *) mgos_sys_config_get_ts3(),
    };

    for (size_t i = 0; i < std::min((size_t) MAX_TS_NUM, sensors.size()); i++) {
      auto *ts_cfg = ts_cfgs[i];
      CreateHAPTemperatureSensor(i + 1, std::move(sensors[i]), ts_cfg, comps,
                                 accs, svr);
    }
    s_onewire = std::move(ow);
    // LED shares the same pin, we need to disable it for OW to work.
    SetSysLEDEnable(false);
  } else {
    SetSysLEDEnable(true);
  }
}

}  // namespace shelly
