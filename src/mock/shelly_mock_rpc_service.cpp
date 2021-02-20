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

#include "shelly_mock.hpp"

#include <cmath>

#include "mgos_rpc.h"

#include "shelly_mock_pm.hpp"
#include "shelly_mock_temp_sensor.hpp"

namespace shelly {

std::vector<MockPowerMeter *> g_mock_pms;
MockTempSensor *g_mock_sys_temp_sensor = nullptr;

static void MockSetSysTempHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                                  struct mg_rpc_frame_info *fi,
                                  struct mg_str args) {
  float temp = NAN;
  json_scanf(args.p, args.len, ri->args_fmt, &temp);
  g_mock_sys_temp_sensor->SetValue(temp);
  mg_rpc_send_responsef(ri, nullptr);
  (void) fi;
  (void) cb_arg;
}

static void MockSetPM(struct mg_rpc_request_info *ri, void *cb_arg,
                      struct mg_rpc_frame_info *fi, struct mg_str args) {
  int id = -1;
  float w = NAN, wh = NAN;
  json_scanf(args.p, args.len, ri->args_fmt, &id, &w, &wh);
  if (id < 0) {
    mg_rpc_send_errorf(ri, 400, "%s is required", "id");
    return;
  }
  if (std::isnan(w) && std::isnan(wh)) {
    mg_rpc_send_errorf(ri, 400, "%s is required", "w or wh");
    return;
  }
  for (auto *pm : g_mock_pms) {
    if (pm->id() == id) {
      if (!std::isnan(w)) pm->SetPowerW(w);
      if (!std::isnan(wh)) pm->SetEnergyWH(wh);
      mg_rpc_send_responsef(ri, nullptr);
      return;
    }
  }
  mg_rpc_send_errorf(ri, 404, "pm %d not found", id);
  (void) fi;
  (void) cb_arg;
}

void MockRPCInit() {
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.Mock.SetSysTemp",
                     "{temp: %f}", MockSetSysTempHandler, nullptr);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.Mock.SetPM",
                     "{id: %d, w: %f, wh: %f}", MockSetPM, nullptr);
}

}  // namespace shelly
