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

#include "mgos.hpp"

#include "nvs_flash.h"
#include "nvs_handle.hpp"

#include "shelly_dht_sensor.hpp"
#include "shelly_hap_garage_door_opener.hpp"
#include "shelly_hap_input.hpp"
#include "shelly_hap_temperature_sensor.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_pm_bl0937.hpp"
#include "shelly_sys_led_btn.hpp"
#include "shelly_temp_sensor_ntc.hpp"
#include "shelly_temp_sensor_ow.hpp"

#ifdef UART_TX_GPIO
#include "shelly_pm_bl0942.hpp"
#endif

namespace shelly {

static std::unique_ptr<Onewire> s_onewire;
static std::vector<std::unique_ptr<TempSensor>> sensors;

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, RELAY1_GPIO, 1));
  auto *in = new InputPin(1, SWITCH1_GPIO, 1, MGOS_GPIO_PULL_NONE, true);
  in->AddHandler(std::bind(&HandleInputResetSequence, in, LED_GPIO, _1, _2));
  in->Init();
  inputs->emplace_back(in);

#ifndef UART_TX_GPIO
  std::unique_ptr<PowerMeter> pm(
      new BL0937PowerMeter(1, 5 /* CF */, 18 /* CF1 */, 23 /* SEL */, 2,
                           mgos_sys_config_get_bl0937_0_apower_scale()));
#else
  std::unique_ptr<PowerMeter> pm(
      new BL0942PowerMeter(1, UART_TX_GPIO, UART_RX_GPIO, 1, 1));
#endif
  const Status &st = pm->Init();
  if (st.ok()) {
    pms->emplace_back(std::move(pm));
  } else {
    const std::string &s = st.ToString();
    LOG(LL_ERROR, ("PM init failed: %s", s.c_str()));
  }

  sys_temp->reset(new TempSensorSDNT1608X103F3950(ADC_GPIO, 3.3f, 10000.0f));

  int pin_out = ADDON_OUT_GPIO;
  int pin_in = ADDON_IN_GPIO;

  if (DetectAddon(pin_in, pin_out)) {
    s_onewire.reset(new Onewire(pin_in, pin_out));
    sensors = s_onewire->DiscoverAll();
    if (sensors.empty()) {
      s_onewire.reset();
      sensors = DiscoverDHTSensors(pin_in, pin_out);
    }

    auto *in2 = new InputPin(2, ADDON_DIG_GPIO, 0, MGOS_GPIO_PULL_NONE, false);
    in2->Init();
    inputs->emplace_back(in2);

  } else {
    RestoreUART();
    InitSysLED(LED_GPIO, LED_ON);
  }
  InitSysBtn(BTN_GPIO, BTN_DOWN);
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  bool gdo_mode = mgos_sys_config_get_shelly_mode() == (int) Mode::kGarageDoor;
  bool ext_sensor_switch = (FindInput(2) != nullptr);
  bool addon_input = !gdo_mode && ext_sensor_switch;
  bool single_accessory =
      sensors.empty() && !addon_input &&
      (mgos_sys_config_get_sw1_in_mode() != (int) InMode::kDetached);
  if (gdo_mode) {
    hap::CreateHAPGDO(1, FindInput(1), FindInput(2), FindOutput(1),
                      FindOutput(1), mgos_sys_config_get_gdo1(), comps, accs,
                      svr, true);
  } else {
    CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                    comps, accs, svr, single_accessory);
  }

  if (!sensors.empty()) {
    CreateHAPSensors(&sensors, comps, accs, svr);
  }
  if (addon_input) {
    hap::CreateHAPInput(2, mgos_sys_config_get_in2(), comps, accs, svr);
  }
}

}  // namespace shelly
