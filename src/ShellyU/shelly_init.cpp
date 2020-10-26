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

#include <cmath>

#include "mgos_rpc.h"
#include "mgos_sys_config.h"

#include "shelly_main.hpp"
#include "shelly_mock_temp_sensor.hpp"

namespace shelly {

static MockTempSensor *s_mock_sys_temp_sensor = nullptr;

static void SetSysTempHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                              struct mg_rpc_frame_info *fi,
                              struct mg_str args) {
  float temp = SNANF;
  json_scanf(args.p, args.len, ri->args_fmt, &temp);
  s_mock_sys_temp_sensor->SetValue(temp);
  mg_rpc_send_responsef(ri, nullptr);
  (void) fi;
  (void) cb_arg;
}

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  outputs->emplace_back(new OutputPin(1, 123, 1));
  s_mock_sys_temp_sensor = new MockTempSensor(33);
  sys_temp->reset(s_mock_sys_temp_sensor);

  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.TestSetSysTemp",
                     "{temp: %f}", SetSysTempHandler, nullptr);
  (void) inputs;
  (void) pms;
}

void CreateComponents(std::vector<Component *> *comps,
                      std::vector<std::unique_ptr<hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  CreateHAPSwitch(1, mgos_sys_config_get_sw1(), nullptr, comps, accs, svr,
                  true /* to_pri_acc */);
}

}  // namespace shelly
