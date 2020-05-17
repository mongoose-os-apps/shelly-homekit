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

#include <stdint.h>

#include "mgos_timers.h"

#include "HAP.h"

#include "shelly_sw_service.h"

struct shelly_sw_service_ctx {
  const struct mgos_config_sw *cfg;
  HAPAccessoryServerRef *hap_server;
  const HAPAccessory *hap_accessory;
  const HAPService *hap_service;
  struct shelly_sw_info info;
  bool pb_state;
  int change_cnt;         // State change counter for reset.
  double last_change_ts;  // Timestamp of last change (uptime).
  mgos_timer_id auto_off_timer_id;
#ifdef MGOS_HAVE_ADE7953
  struct mgos_ade7953 *ade7953;
  int ade7953_channel;
#endif
};

struct shelly_sw_service_ctx *find_ctx(const HAPService *svc);
void shelly_sw_set_state_ctx(struct shelly_sw_service_ctx *ctx, bool new_state,
                             const char *source);

const HAPCharacteristic *shelly_sw_name_char(uint16_t iid);
const HAPCharacteristic *shelly_sw_on_char(uint16_t iid);
const HAPCharacteristic *shelly_sw_lock_cur_state(uint16_t iid);
const HAPCharacteristic *shelly_sw_lock_tgt_state(uint16_t iid);
const HAPCharacteristic *shelly_sw_in_use_char(uint16_t iid);
