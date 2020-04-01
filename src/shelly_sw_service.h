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

#include "HAP.h"
#include "mgos_sys_config.h"

#ifdef MGOS_HAVE_ADE7953
struct mgos_ade7953;
#endif

HAPService *shelly_sw_service_create(
#ifdef MGOS_HAVE_ADE7953
    struct mgos_ade7953 *ade7953, int ade7953_channel,
#endif
    const struct mgos_config_sw *cfg);

struct shelly_sw_info {
  bool state;  // On/off
#ifdef SHELLY_HAVE_PM
  float apower;   // Active power, Watts.
  float aenergy;  // Accumulated active power, Watt-hours.
#endif
};
bool shelly_sw_get_info(int id, struct shelly_sw_info *info);

bool shelly_sw_set_state(int id, bool new_state, const char *source);
