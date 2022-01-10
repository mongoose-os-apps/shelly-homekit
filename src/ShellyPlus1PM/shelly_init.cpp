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

#include "shelly_hap_garage_door_opener.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_pm_bl0937.hpp"
#include "shelly_temp_sensor_ntc.hpp"

namespace shelly {

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
  // Single switch with non-detached input = only one accessory.
  bool to_pri_acc = (mgos_sys_config_get_sw1_in_mode() != 3);
  CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                  comps, accs, svr, to_pri_acc);
}

}  // namespace shelly
