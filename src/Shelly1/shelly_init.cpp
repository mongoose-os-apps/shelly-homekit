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

#include "mgos_hap.h"

#include "shelly_hap_garage_door_opener.hpp"
#include "shelly_hap_temperature_sensor.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_temp_sensor_ow.hpp"

#define MAX_TS_NUM 3

namespace shelly {

static std::unique_ptr<Onewire> s_onewire;

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms UNUSED_ARG,
                       std::unique_ptr<TempSensor> *sys_temp UNUSED_ARG) {
  outputs->emplace_back(new OutputPin(1, 4, 1));
  auto *in = new InputPin(1, 5, 1, MGOS_GPIO_PULL_NONE, true);
  in->AddHandler(std::bind(&HandleInputResetSequence, in, 4, _1, _2));
  in->Init();
  inputs->emplace_back(in);

  s_onewire.reset(new Onewire(3, 0));
  if (s_onewire->DiscoverAll().empty()) {
    s_onewire.reset();
  }
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  // Garage door opener mode.
  if (mgos_sys_config_get_shelly_mode() == 2) {
    auto *gdo_cfg = (struct mgos_config_gdo *) mgos_sys_config_get_gdo1();
    std::unique_ptr<hap::GarageDoorOpener> gdo(
        new hap::GarageDoorOpener(1, FindInput(1), nullptr /* in_open */,
                                  FindOutput(1), FindOutput(1), gdo_cfg));
    if (gdo == nullptr) return;
    auto st = gdo->Init();
    if (!st.ok()) {
      LOG(LL_ERROR, ("GDO init failed: %s", st.ToString().c_str()));
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
  std::vector<std::unique_ptr<TempSensor>> sensors;
  if (s_onewire != nullptr) {
    sensors = s_onewire->DiscoverAll();
  }

  // Single switch with non-detached input and no sensors = only one accessory.
  bool to_pri_acc = (sensors.empty() && (mgos_sys_config_get_sw1_in_mode() !=
                                         (int) InMode::kDetached) && (mgos_sys_config_get_sw1_in_mode() !=
                                         (int) InMode::kDetachedWithRelay));
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
  }
}

}  // namespace shelly
