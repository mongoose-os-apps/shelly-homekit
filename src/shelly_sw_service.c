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

#include "shelly_sw_service.h"

#include "mgos.h"

#define IID_BASE 0x100
#define IID_STEP 4

struct shelly_sw_service_ctx {
  const struct mgos_config_sw *cfg;
  HAPService *svc;
};

static struct shelly_sw_service_ctx s_ctx[NUM_SWITCHES];

static const HAPCharacteristic *shelly_sw_name_char(uint16_t iid) {
  HAPStringCharacteristic *c = calloc(1, sizeof(*c));
  if (c == NULL) return NULL;
  *c = (const HAPStringCharacteristic){
      .format = kHAPCharacteristicFormat_String,
      .iid = iid,
      .characteristicType = &kHAPCharacteristicType_Name,
      .debugDescription = kHAPCharacteristicDebugDescription_Name,
      .manufacturerDescription = NULL,
      .properties =
          {
              .readable = true,
              .writable = false,
              .supportsEventNotification = false,
              .hidden = false,
              .requiresTimedWrite = false,
              .supportsAuthorizationData = false,
              .ip =
                  {
                      .controlPoint = false,
                      .supportsWriteResponse = false,
                  },
              .ble =
                  {
                      .supportsBroadcastNotification = false,
                      .supportsDisconnectedNotification = false,
                      .readableWithoutSecurity = false,
                      .writableWithoutSecurity = false,
                  },
          },
      .constraints = {.maxLength = 64},
      .callbacks = {.handleRead = HAPHandleNameRead, .handleWrite = NULL},
  };
  return c;
};

static struct shelly_sw_service_ctx *find_ctx(const HAPService *svc) {
  for (size_t i = 0; i < ARRAY_SIZE(s_ctx); i++) {
    if (s_ctx[i].svc == svc) return &s_ctx[i];
  }
  return NULL;
}

HAPError shelly_sw_handle_on_read(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicReadRequest *request, bool *value,
    void *context) {
  struct shelly_sw_service_ctx *ctx = find_ctx(request->service);
  const struct mgos_config_sw *cfg = ctx->cfg;
  LOG(LL_INFO, ("%s: READ -> %d", cfg->name, cfg->state));
  *value = cfg->state;
  (void) server;
  (void) context;
  return kHAPError_None;
}

HAPError shelly_sw_handle_on_write(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicWriteRequest *request, bool value,
    void *context) {
  struct shelly_sw_service_ctx *ctx = find_ctx(request->service);
  const struct mgos_config_sw *cfg = ctx->cfg;
  LOG(LL_INFO, ("%s: WRITE %d", cfg->name, value));
  mgos_gpio_write(cfg->out_gpio, value);
  ((struct mgos_config_sw *) cfg)->state = value;
  mgos_sys_config_save(&mgos_sys_config, false /* try_once */, NULL /* msg */);
  (void) server;
  (void) context;
  return kHAPError_None;
}

static const HAPCharacteristic *shelly_sw_on_char(uint16_t iid) {
  HAPBoolCharacteristic *c = calloc(1, sizeof(*c));
  if (c == NULL) return NULL;
  *c = (const HAPBoolCharacteristic){
      .format = kHAPCharacteristicFormat_Bool,
      .iid = iid,
      .characteristicType = &kHAPCharacteristicType_On,
      .debugDescription = kHAPCharacteristicDebugDescription_On,
      .manufacturerDescription = NULL,
      .properties =
          {
              .readable = true,
              .writable = true,
              .supportsEventNotification = true,
              .hidden = false,
              .requiresTimedWrite = false,
              .supportsAuthorizationData = false,
              .ip = {.controlPoint = false, .supportsWriteResponse = false},
              .ble =
                  {
                      .supportsBroadcastNotification = true,
                      .supportsDisconnectedNotification = true,
                      .readableWithoutSecurity = false,
                      .writableWithoutSecurity = false,
                  },
          },
      .callbacks =
          {
              .handleRead = shelly_sw_handle_on_read,
              .handleWrite = shelly_sw_handle_on_write,
          },
  };
  return c;
};

HAPService *shelly_sw_service_create(const struct mgos_config_sw *cfg) {
  if (cfg->id >= NUM_SWITCHES) {
    LOG(LL_ERROR, ("Switch ID too big!"));
    return NULL;
  }
  HAPService *svc = calloc(1, sizeof(*svc));
  if (svc == NULL) return NULL;
  const HAPCharacteristic **chars = calloc(3, sizeof(*chars));
  if (chars == NULL) return NULL;
  svc->iid = IID_BASE + (IID_STEP * cfg->id) + 0;
  svc->serviceType = &kHAPServiceType_Switch;
  svc->debugDescription = kHAPServiceDebugDescription_Switch;
  svc->name = cfg->name;
  svc->properties.primaryService = true;
  chars[0] = shelly_sw_name_char(IID_BASE + (IID_STEP * cfg->id) + 1);
  chars[1] = shelly_sw_on_char(IID_BASE + (IID_STEP * cfg->id) + 2);
  chars[2] = NULL;
  svc->characteristics = chars;
  s_ctx[cfg->id].cfg = cfg;
  s_ctx[cfg->id].svc = svc;
  LOG(LL_INFO,
      ("Exporting '%s' (GPIO out: %d, in: %d, state: %d) %p", cfg->name,
       cfg->out_gpio, cfg->in_gpio, cfg->state, svc->characteristics[1]));
  mgos_gpio_setup_output(cfg->out_gpio, cfg->state);
  return svc;
}
