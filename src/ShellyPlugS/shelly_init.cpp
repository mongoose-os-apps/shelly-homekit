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

#include "shelly_main.hpp"
#include "shelly_output.hpp"
#include "shelly_pm_bl0937.hpp"
#include "shelly_sys_led_btn.hpp"
#include "shelly_temp_sensor_ntc.hpp"

namespace shelly {

static OutputPin *s_led_out = nullptr;

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 15, 1));
  s_led_out = new OutputPin(99, 0, 0);  // Red LED.
  std::unique_ptr<PowerMeter> pm(
      new BL0937PowerMeter(1, 5 /* CF */, 14 /* CF1 */, 12 /* SEL */, 2,
                           mgos_sys_config_get_bl0937_power_coeff()));
  const Status &st = pm->Init();
  if (st.ok()) {
    pms->emplace_back(std::move(pm));
  } else {
    const std::string &s = st.ToString();
    LOG(LL_ERROR, ("PM init failed: %s", s.c_str()));
  }
  sys_temp->reset(new TempSensorSDNT1608X103F3950(0, 3.3f, 33000.0f));

  InitSysLED(LED_GPIO, LED_ON);
  InitSysBtn(BTN_GPIO, BTN_DOWN);
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  CreateHAPSwitch(1, mgos_sys_config_get_sw1(), nullptr, comps, accs, svr,
                  true /* to_pri_acc */, s_led_out);
}

}  // namespace shelly
