/*
 * Copyright (c) 2020 Deomid "rojer" Ryabkov
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

#include "shelly_component.hpp"
#include "shelly_hap_service.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"
#include "shelly_pm.hpp"

namespace shelly {

extern std::vector<std::unique_ptr<Component>> g_components;

Input *FindInput(int id);
Output *FindOutput(int id);
PowerMeter *FindPM(int id);

void CreateHAPSwitch(int id, const struct mgos_config_sw *sw_cfg,
                     const struct mgos_config_ssw *ssw_cfg,
                     std::vector<std::unique_ptr<Component>> *components,
                     std::vector<const HAPService *> *services,
                     hap::ServiceLabelService *sls,
                     HAPAccessoryServerRef *server, HAPAccessory *accessory);

void HandleInputResetSequence(InputPin *in, int out_gpio, Input::Event ev,
                              bool cur_state);

void RestartHAPServer();

// Implemented for each model.

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms);

void CreateComponents(std::vector<std::unique_ptr<Component>> *components,
                      std::vector<const HAPService *> *services,
                      hap::ServiceLabelService *sls,
                      HAPAccessoryServerRef *server, HAPAccessory *accessory);

}  // namespace shelly
