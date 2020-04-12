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
 *limitations under the License.
 */

#include "shelly_sw_service.h"

#include <math.h>

#include "mgos.h"
#ifdef MGOS_HAVE_ADE7953
#include "mgos_ade7953.h"
#endif

#define IID_BASE 0x100
#define IID_STEP 4

enum shelly_sw_in_mode {
  SHELLY_SW_IN_MODE_MOMENTARY = 0,
  SHELLY_SW_IN_MODE_TOGGLE = 1,
  SHELLY_SW_IN_MODE_EDGE = 2,
  SHELLY_SW_IN_MODE_DETACHED = 3,
};

struct shelly_sw_service_ctx {
  const struct mgos_config_sw *cfg;
  HAPAccessoryServerRef *hap_server;
  const HAPAccessory *hap_accessory;
  const HAPService *hap_service;
  struct shelly_sw_info info;
  bool pb_state;
  int change_cnt;         // State change counter for reset.
  double last_change_ts;  // Timestamp of last change (uptime).
#ifdef MGOS_HAVE_ADE7953
  struct mgos_ade7953 *ade7953;
  int ade7953_channel;
#endif
};

static struct shelly_sw_service_ctx s_ctx[NUM_SWITCHES];

static int s_auto_off_timer_id = MGOS_INVALID_TIMER_ID;

static void do_auto_off(void *arg);

#ifdef SHELLY_HAVE_PM
static void shelly_sw_read_power(void *arg);
#endif

static void do_reset(void *arg) {
  struct shelly_sw_service_ctx *ctx = arg;
  mgos_gpio_blink(ctx->cfg->out_gpio, 0, 0);
  LOG(LL_INFO, ("Performing reset"));
#ifdef MGOS_SYS_CONFIG_HAVE_WIFI
  mgos_sys_config_set_wifi_sta_enable(false);
  mgos_sys_config_set_wifi_ap_enable(true);
  mgos_sys_config_save(&mgos_sys_config, false, NULL);
  mgos_wifi_setup((struct mgos_config_wifi *) mgos_sys_config_get_wifi());
#endif
}

static void handle_auto_off(struct shelly_sw_service_ctx *ctx,
                            const char *source, bool new_state) {
  if (s_auto_off_timer_id != MGOS_INVALID_TIMER_ID) {
    // Cancel timer if state changes so that only the last timer is triggered if
    // state changes multiple times
    mgos_clear_timer(s_auto_off_timer_id);
    s_auto_off_timer_id = MGOS_INVALID_TIMER_ID;
  }

  const struct mgos_config_sw *cfg = ctx->cfg;

  if (!cfg->auto_off) return;

  if (strcmp(source, "auto_off") == 0) return;

  if (!new_state) return;

  s_auto_off_timer_id =
      mgos_set_timer(cfg->auto_off_delay * 1000, 0, do_auto_off, ctx);
}

static void shelly_sw_set_state_ctx(struct shelly_sw_service_ctx *ctx,
                                    bool new_state, const char *source) {
  const struct mgos_config_sw *cfg = ctx->cfg;
  if (new_state == ctx->info.state) return;
  int out_value = (new_state ? cfg->out_on_value : !cfg->out_on_value);
  mgos_gpio_write(cfg->out_gpio, out_value);
  LOG(LL_INFO, ("%s: %d -> %d (%s) %d", cfg->name, ctx->info.state, new_state,
                source, out_value));
  ctx->info.state = new_state;
  if (ctx->hap_server != NULL) {
    HAPAccessoryServerRaiseEvent(ctx->hap_server,
                                 ctx->hap_service->characteristics[1],
                                 ctx->hap_service, ctx->hap_accessory);
  }
  if (cfg->persist_state) {
    ((struct mgos_config_sw *) cfg)->state = new_state;
    mgos_sys_config_save(&mgos_sys_config, false /* try_once */,
                         NULL /* msg */);
  }

  double now = mgos_uptime();
  if (now < 60) {
    if (now - ctx->last_change_ts > 10) {
      ctx->change_cnt = 0;
    }
    ctx->change_cnt++;
    ctx->last_change_ts = now;
    if (ctx->change_cnt >= 10) {
      LOG(LL_INFO, ("Reset sequence detected"));
      ctx->change_cnt = 0;
      mgos_gpio_blink(cfg->out_gpio, 100, 100);
      mgos_set_timer(600, 0, do_reset, ctx);
    }
  }

  handle_auto_off(ctx, source, new_state);
}

static void do_auto_off(void *arg) {
  s_auto_off_timer_id = MGOS_INVALID_TIMER_ID;
  struct shelly_sw_service_ctx *ctx = arg;
  const struct mgos_config_sw *cfg = ctx->cfg;
  if (cfg->auto_off) {
    // Don't set state if auto off has been disabled during timer run
    shelly_sw_set_state_ctx(ctx, false, "auto_off");
  }
}

bool shelly_sw_set_state(int id, bool new_state, const char *source) {
  if (id < 0 || id >= NUM_SWITCHES) return false;
  struct shelly_sw_service_ctx *ctx = &s_ctx[id];
  if (ctx == NULL) return false;
  shelly_sw_set_state_ctx(ctx, new_state, source);
  return true;
}

bool shelly_sw_get_info(int id, struct shelly_sw_info *info) {
  if (id < 0 || id >= NUM_SWITCHES) return false;
  struct shelly_sw_service_ctx *ctx = &s_ctx[id];
  if (ctx == NULL) return false;
  *info = ctx->info;
  return true;
}

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
    if (s_ctx[i].hap_service == svc) return &s_ctx[i];
  }
  return NULL;
}

HAPError shelly_sw_handle_on_read(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicReadRequest *request, bool *value,
    void *context) {
  struct shelly_sw_service_ctx *ctx = find_ctx(request->service);
  const struct mgos_config_sw *cfg = ctx->cfg;
  *value = ctx->info.state;
  LOG(LL_INFO, ("%s: READ -> %d", cfg->name, ctx->info.state));
  ctx->hap_server = server;
  ctx->hap_accessory = request->accessory;
  (void) context;
  return kHAPError_None;
}

HAPError shelly_sw_handle_on_write(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicWriteRequest *request, bool value,
    void *context) {
  struct shelly_sw_service_ctx *ctx = find_ctx(request->service);
  ctx->hap_server = server;
  ctx->hap_accessory = request->accessory;
  shelly_sw_set_state_ctx(ctx, value, "HAP");
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

static void shelly_sw_in_cb(int pin, void *arg) {
  struct shelly_sw_service_ctx *ctx = arg;
  bool in_state = mgos_gpio_read(pin);
  switch ((enum shelly_sw_in_mode) ctx->cfg->in_mode) {
    case SHELLY_SW_IN_MODE_MOMENTARY:
      if (in_state) {  // Only on 0 -> 1 transitions.
        shelly_sw_set_state_ctx(ctx, !ctx->info.state, "button");
      }
      break;
    case SHELLY_SW_IN_MODE_TOGGLE:
      shelly_sw_set_state_ctx(ctx, in_state, "switch");
      break;
    case SHELLY_SW_IN_MODE_EDGE:
      shelly_sw_set_state_ctx(ctx, !ctx->info.state, "button");
      break;
    case SHELLY_SW_IN_MODE_DETACHED:
      // Nothing to do
      break;
  }
  (void) pin;
}

#ifdef SHELLY_HAVE_PM
static void shelly_sw_read_power(void *arg) {
  struct shelly_sw_service_ctx *ctx = arg;
#ifdef MGOS_HAVE_ADE7953
  float apa = 0, aea = 0;
  if (mgos_ade7953_get_apower(ctx->ade7953, ctx->ade7953_channel, &apa)) {
    if (fabs(apa) < 0.5) apa = 0;  // Suppress noise.
    ctx->info.apower = fabs(apa);
  }
  if (mgos_ade7953_get_aenergy(ctx->ade7953, ctx->ade7953_channel,
                               true /* reset */, &aea)) {
    ctx->info.aenergy += fabs(aea);
  }
#endif
}
#endif

HAPService *shelly_sw_service_create(
#ifdef MGOS_HAVE_ADE7953
    struct mgos_ade7953 *ade7953, int ade7953_channel,
#endif
    const struct mgos_config_sw *cfg) {
  if (!cfg->enable) {
    LOG(LL_INFO, ("'%s' is disabled", cfg->name));
    mgos_gpio_setup_output(cfg->out_gpio, !cfg->out_on_value);
    return NULL;
  }
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
  struct shelly_sw_service_ctx *ctx = &s_ctx[cfg->id];
  ctx->cfg = cfg;
  ctx->hap_service = svc;
  if (cfg->persist_state) {
    ctx->info.state = cfg->state;
  } else {
    ctx->info.state = 0;
  }
  LOG(LL_INFO, ("Exporting '%s' (GPIO out: %d, in: %d, state: %d)", cfg->name,
                cfg->out_gpio, cfg->in_gpio, ctx->info.state));
  mgos_gpio_setup_output(cfg->out_gpio, (ctx->info.state ? cfg->out_on_value
                                                         : !cfg->out_on_value));
  mgos_gpio_set_button_handler(cfg->in_gpio, MGOS_GPIO_PULL_NONE,
                               MGOS_GPIO_INT_EDGE_ANY, 20, shelly_sw_in_cb,
                               ctx);
  if (ctx->cfg->in_mode == SHELLY_SW_IN_MODE_TOGGLE) {
    shelly_sw_in_cb(cfg->in_gpio, ctx);
  }
#ifdef SHELLY_HAVE_PM
#ifdef MGOS_HAVE_ADE7953
  ctx->ade7953 = ade7953;
  ctx->ade7953_channel = ade7953_channel;
#endif
  mgos_set_timer(1000, MGOS_TIMER_REPEAT, shelly_sw_read_power, ctx);
#endif
  return svc;
}
