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

#pragma once

#include <memory>
#include <vector>

#include "mgos_sys_config.h"

#include "mgos_hap_accessory.hpp"
#include "mgos_hap_service.hpp"
#include "shelly_component.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"
#include "shelly_pm.hpp"
#include "shelly_reset.hpp"
#include "shelly_temp_sensor.hpp"

#define AUTH_USER "admin"
#define AUTH_FILE_NAME "passwd256"
#define ACL_FILE_NAME "rpc_acl.json"
#define KVS_FILE_NAME "kvs.json"

namespace shelly {

extern std::vector<std::unique_ptr<Component>> g_comps;

Input *FindInput(int id);
Output *FindOutput(int id);
PowerMeter *FindPM(int id);

void CreateHAPSwitch(int id, const struct mgos_config_sw *sw_cfg,
                     const struct mgos_config_in *in_cfg,
                     std::vector<std::unique_ptr<Component>> *comps,
                     std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                     HAPAccessoryServerRef *svr, bool to_pri_acc,
                     Output *led_out = nullptr);

void HandleInputResetSequence(Input *in, int out_gpio, Input::Event ev,
                              bool cur_state);

void RestartService();

StatusOr<int> GetSystemTemperature();

#define SHELLY_SERVICE_FLAG_UPDATE (1 << 0)
#define SHELLY_SERVICE_FLAG_REBOOT (1 << 1)
#define SHELLY_SERVICE_FLAG_OVERHEAT (1 << 2)
#define SHELLY_SERVICE_FLAG_REVERT (1 << 3)
uint8_t GetServiceFlags();

HAPError AccessoryIdentifyCB(const HAPAccessoryIdentifyRequest *request);

int GetOTAProgress();

// Implemented for each model.

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp);

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr);

}  // namespace shelly
