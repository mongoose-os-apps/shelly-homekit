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

#include <functional>
#include <memory>
#include <string>

#include "common/cs_dbg.h"
#include "common/util/status.h"
#include "common/util/statusor.h"

#define SHELLY_HAP_AID_PRIMARY 0x1
#define SHELLY_HAP_AID_BASE_SWITCH 0x100
#define SHELLY_HAP_AID_BASE_OUTLET 0x200
#define SHELLY_HAP_AID_BASE_LOCK 0x300
#define SHELLY_HAP_AID_BASE_STATELESS_SWITCH 0x400
#define SHELLY_HAP_AID_BASE_WINDOW_COVERING 0x500
#define SHELLY_HAP_AID_BASE_MOTION_SENSOR 0x600
#define SHELLY_HAP_AID_BASE_OCCUPANCY_SENSOR 0x700
#define SHELLY_HAP_AID_BASE_CONTACT_SENSOR 0x800
#define SHELLY_HAP_AID_BASE_VALVE 0x900

#define SHELLY_HAP_IID_BASE_SWITCH 0x100
#define SHELLY_HAP_IID_STEP_SWITCH 4
#define SHELLY_HAP_IID_BASE_OUTLET 0x200
#define SHELLY_HAP_IID_STEP_OUTLET 5
#define SHELLY_HAP_IID_BASE_LOCK 0x300
#define SHELLY_HAP_IID_STEP_LOCK 4
#define SHELLY_HAP_IID_BASE_STATELESS_SWITCH 0x400
#define SHELLY_HAP_IID_STEP_STATELESS_SWITCH 4
#define SHELLY_HAP_IID_BASE_WINDOW_COVERING 0x500
#define SHELLY_HAP_IID_STEP_WINDOW_COVERING 0x10
#define SHELLY_HAP_IID_BASE_GARAGE_DOOR_OPENER 0x600
#define SHELLY_HAP_IID_STEP_GARAGE_DOOR_OPENER 0x10
#define SHELLY_HAP_IID_BASE_MOTION_SENSOR 0x700
#define SHELLY_HAP_IID_BASE_OCCUPANCY_SENSOR 0x800
#define SHELLY_HAP_IID_BASE_CONTACT_SENSOR 0x900
#define SHELLY_HAP_IID_STEP_SENSOR 0x10
#define SHELLY_HAP_IID_BASE_VALVE 0xa00
#define SHELLY_HAP_IID_STEP_VALVE 0x10

namespace shelly {

using mgos::Status;
using mgos::StatusOr;

using namespace std::placeholders;  // _1, _2, ...

inline const char *OnOff(bool on) {
  return (on ? "on" : "off");
}

inline const char *YesNo(bool yes) {
  return (yes ? "yes" : "no");
}

// TODO(rojer): Move upstream.
#define LOG_EVERY_N(l, n, x)                                              \
  do {                                                                    \
    static int cnt = 0;                                                   \
    if (cnt++ % (n) == 0 && cs_log_print_prefix(l, __FILE__, __LINE__)) { \
      cs_log_printf x;                                                    \
    }                                                                     \
  } while (0)

}  // namespace shelly
