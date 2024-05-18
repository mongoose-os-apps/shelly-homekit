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

#include "shelly_dht_sensor.hpp"
#include "shelly_hap_garage_door_opener.hpp"
#include "shelly_hap_input.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_temp_sensor_ow.hpp"

namespace shelly {

static std::unique_ptr<Onewire> s_onewire;
static std::vector<std::unique_ptr<TempSensor>> sensors;

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms UNUSED_ARG,
                       std::unique_ptr<TempSensor> *sys_temp UNUSED_ARG) {
  outputs->emplace_back(new OutputPin(1, 4, 1));
  auto *in1 = new InputPin(1, 5, 1, MGOS_GPIO_PULL_NONE, true);
  in1->AddHandler(std::bind(&HandleInputResetSequence, in1, 4, _1, _2));
  in1->Init();
  inputs->emplace_back(in1);

  int pin_in = 3;
  int pin_out = 0;

  if (DetectAddon(pin_in, pin_out)) {
    s_onewire.reset(new Onewire(pin_in, pin_out));
    sensors = s_onewire->DiscoverAll();
    if (sensors.empty()) {
      s_onewire.reset();
      sensors = DiscoverDHTSensors(pin_in, pin_out);
    }
    if (sensors.empty()) {
      // No sensors detected, we assume to use addon as input for switch or
      // closed/open sensor
      auto *in2 = new InputPin(2, pin_in, 0, MGOS_GPIO_PULL_NONE, false);
      in2->Init();
      inputs->emplace_back(in2);
    }
  }
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  bool gdo_mode = mgos_sys_config_get_shelly_mode() == (int) Mode::kGarageDoor;
  bool ext_sensor_switch = (FindInput(2) != nullptr);
  bool detatched_sensor =
      (mgos_sys_config_get_sw1_in_mode() != (int) InMode::kDetached) &&
      !gdo_mode && ext_sensor_switch;
  bool single_accessory = sensors.empty() && !detatched_sensor;
  if (gdo_mode) {
    hap::CreateHAPGDO(1, FindInput(1), FindInput(2), FindOutput(1),
                      FindOutput(1), mgos_sys_config_get_gdo1(), comps, accs,
                      svr, single_accessory);
  } else {
    CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                    comps, accs, svr, single_accessory);
  }

  if (!sensors.empty()) {
    CreateHAPSensors(&sensors, comps, accs, svr);
  } else if (detatched_sensor) {
    hap::CreateHAPInput(2, mgos_sys_config_get_in2(), comps, accs, svr);
  }
}

}  // namespace shelly
