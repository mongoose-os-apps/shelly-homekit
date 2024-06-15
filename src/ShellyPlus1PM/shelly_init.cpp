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

namespace shelly {

static std::unique_ptr<Onewire> s_onewire;
static std::vector<std::unique_ptr<TempSensor>> sensors;

static constexpr const char *kNVSPartitionName = "shelly";
static constexpr const char *kNVSNamespace = "shelly";
static constexpr const char *kAPowerCoeffNVSKey = "Pm0.apower";

static StatusOr<float> ReadPowerCoeff() {
  esp_err_t err = ESP_OK;
  auto fh = nvs::open_nvs_handle_from_partition(
      kNVSPartitionName, kNVSNamespace, NVS_READONLY, &err);
  if (fh == nullptr) {
    return mgos::Errorf(STATUS_NOT_FOUND, "No NVS factory data! err %d", err);
  }
  size_t size = 0;
  err = fh->get_item_size(nvs::ItemType::SZ, kAPowerCoeffNVSKey, size);
  if (err != ESP_OK) {
    return mgos::Errorf(STATUS_NOT_FOUND, "No power calibration data!");
  }
  char *buf = (char *) calloc(1, size + 1);  // NUL at the end.
  if (buf == nullptr) {
    return mgos::Errorf(STATUS_RESOURCE_EXHAUSTED, "Out of memory");
  }
  mgos::ScopedCPtr buf_owner(buf);
  err = fh->get_string(kAPowerCoeffNVSKey, buf, size);
  if (err != ESP_OK) {
    return mgos::Errorf(STATUS_RESOURCE_EXHAUSTED, "Failed to read key: %d",
                        err);
  }
  float apc = atof(buf);
  LOG(LL_DEBUG, ("Factory apower calibration value: %f", apc));
  return apc;
}

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  nvs_flash_init_partition(kNVSPartitionName);
  outputs->emplace_back(new OutputPin(1, 26, 1));
  auto *in = new InputPin(1, 4, 1, MGOS_GPIO_PULL_NONE, true);
  in->AddHandler(std::bind(&HandleInputResetSequence, in, LED_GPIO, _1, _2));
  in->Init();
  inputs->emplace_back(in);

  // Read factory calibration data but only if the value is default.
  // If locally adjusted, do not override.
  if (mgos_sys_config_get_bl0937_0_apower_scale() ==
      mgos_sys_config_get_default_bl0937_0_apower_scale()) {
    auto apcs = ReadPowerCoeff();
    if (apcs.ok()) {
      mgos_sys_config_set_bl0937_0_apower_scale(apcs.ValueOrDie());
    } else {
      auto ss = apcs.status().ToString();
      LOG(LL_ERROR, ("Error reading factory calibration data: %s", ss.c_str()));
    }
  }
  std::unique_ptr<PowerMeter> pm(
      new BL0937PowerMeter(1, 5 /* CF */, 18 /* CF1 */, 23 /* SEL */, 2,
                           mgos_sys_config_get_bl0937_0_apower_scale()));
  const Status &st = pm->Init();
  if (st.ok()) {
    pms->emplace_back(std::move(pm));
  } else {
    const std::string &s = st.ToString();
    LOG(LL_ERROR, ("PM init failed: %s", s.c_str()));
  }
  sys_temp->reset(new TempSensorSDNT1608X103F3950(32, 3.3f, 10000.0f));

  int pin_out = 0;
  int pin_in = 1;

  if (DetectAddon(pin_in, pin_out)) {
    s_onewire.reset(new Onewire(pin_in, pin_out));
    sensors = s_onewire->DiscoverAll();
    if (sensors.empty()) {
      s_onewire.reset();
      sensors = DiscoverDHTSensors(pin_in, pin_out);
    }

    auto *in2 = new InputPin(2, 19, 0, MGOS_GPIO_PULL_NONE, false);
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
