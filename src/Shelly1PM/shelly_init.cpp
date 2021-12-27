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

#define NUM_SENSORS_MAX 3

namespace shelly {

static std::unique_ptr<Onewire> onewire;

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 15, 1));
  auto *in = new InputPin(1, 4, 1, MGOS_GPIO_PULL_NONE, true);
  in->AddHandler(std::bind(&HandleInputResetSequence, in, 15, _1, _2));
  in->Init();
  inputs->emplace_back(in);

  onewire.reset(new Onewire(3, 0));

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
  std::vector<std::unique_ptr<TempSensor>> sensors;
  onewire->DiscoverAll(NUM_SENSORS_MAX, &sensors);

  // Single switch with non-detached input and no discovered sensor = only one
  // accessory.
  bool to_pri_acc =
      (sensors.size() == 0) && (mgos_sys_config_get_sw1_in_mode() != 3);
  CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                  comps, accs, svr, to_pri_acc);

  struct mgos_config_se *se_cfgs[NUM_SENSORS_MAX] = {
      (struct mgos_config_se *) mgos_sys_config_get_se1(),
      (struct mgos_config_se *) mgos_sys_config_get_se2(),
      (struct mgos_config_se *) mgos_sys_config_get_se3(),
  };

  for (unsigned int i = 0; i < sensors.size(); i++) {
    auto *se_cfg = se_cfgs[i];
    CreateHAPSensor(i + 1, std::move(sensors[i]), se_cfg, comps, accs, svr);
  }
}

}  // namespace shelly
