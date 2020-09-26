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
#include "shelly_hap_accessory.hpp"
#include "shelly_hap_service.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"
#include "shelly_pm.hpp"

namespace shelly {

extern std::vector<Component *> g_comps;

Input *FindInput(int id);
Output *FindOutput(int id);
PowerMeter *FindPM(int id);

void CreateHAPSwitch(int id, const struct mgos_config_sw *sw_cfg,
                     const struct mgos_config_ssw *ssw_cfg,
                     std::vector<Component *> *comps,
                     std::vector<std::unique_ptr<hap::Accessory>> *accs,
                     HAPAccessoryServerRef *svr, bool to_pri_acc);
void CreateHAPStatelessSwitch(
    int id, const struct mgos_config_ssw *ssw_cfg,
    std::vector<Component *> *comps,
    std::vector<std::unique_ptr<hap::Accessory>> *accs,
    HAPAccessoryServerRef *svr);

void HandleInputResetSequence(InputPin *in, int out_gpio, Input::Event ev,
                              bool cur_state);

void RestartHAPServer();

HAPError AccessoryIdentifyCB(const HAPAccessoryIdentifyRequest *request);

// Implemented for each model.

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms);

void CreateComponents(std::vector<Component *> *comps,
                      std::vector<std::unique_ptr<hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr);

}  // namespace shelly
