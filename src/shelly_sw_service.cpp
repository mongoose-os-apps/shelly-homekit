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
#include "shelly_sw_service_internal.h"

#include <math.h>

#include "mgos.h"
#ifdef MGOS_HAVE_ADE7953
#include "mgos_ade7953.h"
#endif

#define IID_BASE_SWITCH 0x100
#define IID_STEP_SWITCH 4
#define IID_BASE_OUTLET 0x200
#define IID_STEP_OUTLET 5
#define IID_BASE_LOCK 0x300
#define IID_STEP_LOCK 4

static struct shelly_sw_service_ctx s_ctx[NUM_SWITCHES];

static void do_auto_off(void *arg);

#ifdef SHELLY_HAVE_PM
static void shelly_sw_read_power(void *arg);
#endif

static void do_reset(void *arg) {
  struct shelly_sw_service_ctx *ctx = (struct shelly_sw_service_ctx *) arg;
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
  if (ctx->auto_off_timer_id != MGOS_INVALID_TIMER_ID) {
    // Cancel timer if state changes so that only the last timer is triggered if
    // state changes multiple times
    mgos_clear_timer(ctx->auto_off_timer_id);
    ctx->auto_off_timer_id = MGOS_INVALID_TIMER_ID;
    LOG(LL_INFO, ("%d: Cleared auto-off timer", ctx->cfg->id));
  }

  const struct mgos_config_sw *cfg = ctx->cfg;

  if (!cfg->auto_off) return;

  if (strcmp(source, "auto_off") == 0) return;

  if (!new_state) return;

  ctx->auto_off_timer_id =
      mgos_set_timer(cfg->auto_off_delay * 1000, 0, do_auto_off, ctx);
  LOG(LL_INFO,
      ("%d: Set auto-off timer for %.3f", cfg->id, cfg->auto_off_delay));
}

void shelly_sw_set_state_ctx(struct shelly_sw_service_ctx *ctx, bool new_state,
                             const char *source) {
  const struct mgos_config_sw *cfg = ctx->cfg;
  int out_value = (new_state ? cfg->out_on_value : !cfg->out_on_value);
  mgos_gpio_setup_output(cfg->out_gpio, out_value);
  if (new_state == ctx->info.state) return;
  LOG(LL_INFO, ("%s: %d -> %d (%s) %d", cfg->name, ctx->info.state, new_state,
                source, out_value));
  ctx->info.state = new_state;
  if (ctx->hap_server != NULL) {
    if (cfg->svc_type == SHELLY_SW_TYPE_LOCK) {
      HAPAccessoryServerRaiseEvent(ctx->hap_server,
                                   ctx->hap_service->characteristics[2],
                                   ctx->hap_service, ctx->hap_accessory);
    }
    HAPAccessoryServerRaiseEvent(ctx->hap_server,
                                 ctx->hap_service->characteristics[1],
                                 ctx->hap_service, ctx->hap_accessory);
  }
  if (cfg->state != new_state) {
    ((struct mgos_config_sw *) cfg)->state = new_state;
    if (cfg->initial_state == SHELLY_SW_INITIAL_STATE_LAST) {
      mgos_sys_config_save(&mgos_sys_config, false /* try_once */,
                           NULL /* msg */);
    }
  }

  handle_auto_off(ctx, source, new_state);
}

static void do_auto_off(void *arg) {
  struct shelly_sw_service_ctx *ctx = (struct shelly_sw_service_ctx *) arg;
  const struct mgos_config_sw *cfg = ctx->cfg;
  ctx->auto_off_timer_id = MGOS_INVALID_TIMER_ID;
  LOG(LL_INFO, ("%d: Auto-off timer fired", cfg->id));
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

struct shelly_sw_service_ctx *find_ctx(const HAPService *svc) {
  for (size_t i = 0; i < ARRAY_SIZE(s_ctx); i++) {
    if (s_ctx[i].hap_service == svc) return &s_ctx[i];
  }
  return NULL;
}

static void shelly_sw_in_cb(int pin, void *arg) {
  struct shelly_sw_service_ctx *ctx = (struct shelly_sw_service_ctx *) arg;
  bool in_state = mgos_gpio_read(pin);

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
      mgos_gpio_blink(ctx->cfg->out_gpio, 100, 100);
      mgos_set_timer(600, 0, do_reset, ctx);
      return;
    }
  }

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
  struct shelly_sw_service_ctx *ctx = (struct shelly_sw_service_ctx *) arg;
#ifdef MGOS_HAVE_ADE7953
  float apa = 0, aea = 0;
  if (mgos_ade7953_get_apower(ctx->ade7953, ctx->ade7953_channel, &apa)) {
    if (fabs(apa) < 1) apa = 0;  // Suppress noise.
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
  HAPService *svc = (HAPService *) calloc(1, sizeof(*svc));
  if (svc == NULL) return NULL;
  svc->name = cfg->name;
  svc->properties.primaryService = true;
  const HAPCharacteristic **chars =
      (const HAPCharacteristic **) calloc(5, sizeof(*chars));
  if (chars == NULL) return NULL;
  uint16_t iid;
  const char *svc_type_name = NULL;
  switch (cfg->svc_type) {
    default:
    case SHELLY_SW_TYPE_SWITCH:
      iid = IID_BASE_SWITCH + (IID_STEP_SWITCH * cfg->id);
      svc->iid = iid++;
      svc->serviceType = &kHAPServiceType_Switch;
      svc->debugDescription = kHAPServiceDebugDescription_Switch;
      chars[0] = shelly_sw_name_char(iid++);
      chars[1] = shelly_sw_on_char(iid++);
      svc_type_name = "switch";
      break;
    case SHELLY_SW_TYPE_OUTLET:
      iid = IID_BASE_OUTLET + (IID_STEP_OUTLET * cfg->id);
      svc->iid = iid++;
      svc->serviceType = &kHAPServiceType_Outlet;
      svc->debugDescription = kHAPServiceDebugDescription_Outlet;
      chars[0] = shelly_sw_name_char(iid++);
      chars[1] = shelly_sw_on_char(iid++);
      chars[2] = shelly_sw_in_use_char(iid++);
      svc_type_name = "outlet";
      break;
    case SHELLY_SW_TYPE_LOCK:
      iid = IID_BASE_LOCK + (IID_STEP_LOCK * cfg->id);
      svc->iid = iid++;
      svc->serviceType = &kHAPServiceType_LockMechanism;
      svc->debugDescription = kHAPServiceDebugDescription_LockMechanism;
      chars[0] = shelly_sw_name_char(iid++);
      chars[1] = shelly_sw_lock_cur_state(iid++);
      chars[2] = shelly_sw_lock_tgt_state(iid++);
      svc_type_name = "lock";
      break;
  }
  svc->characteristics = chars;
  struct shelly_sw_service_ctx *ctx = &s_ctx[cfg->id];
  ctx->cfg = cfg;
  ctx->hap_service = svc;
  ctx->auto_off_timer_id = MGOS_INVALID_TIMER_ID;
  LOG(LL_INFO,
      ("Exporting '%s': type %s, GPIO out: %d, in: %d, state: %d", cfg->name,
       svc_type_name, cfg->out_gpio, cfg->in_gpio, ctx->info.state));

  switch ((enum shelly_sw_initial_state) cfg->initial_state) {
    case SHELLY_SW_INITIAL_STATE_OFF:
      shelly_sw_set_state_ctx(ctx, false, "init");
      break;
    case SHELLY_SW_INITIAL_STATE_ON:
      shelly_sw_set_state_ctx(ctx, true, "init");
      break;
    case SHELLY_SW_INITIAL_STATE_LAST:
      shelly_sw_set_state_ctx(ctx, cfg->state, "init");
      break;
    case SHELLY_SW_INITIAL_STATE_INPUT:
      if (cfg->in_mode == SHELLY_SW_IN_MODE_TOGGLE) {
        shelly_sw_in_cb(cfg->in_gpio, ctx);
      }
  }
  mgos_gpio_set_button_handler(cfg->in_gpio, MGOS_GPIO_PULL_NONE,
                               MGOS_GPIO_INT_EDGE_ANY, 20, shelly_sw_in_cb,
                               ctx);
#ifdef SHELLY_HAVE_PM
#ifdef MGOS_HAVE_ADE7953
  ctx->ade7953 = ade7953;
  ctx->ade7953_channel = ade7953_channel;
#endif
  mgos_set_timer(1000, MGOS_TIMER_REPEAT, shelly_sw_read_power, ctx);
#endif
  return svc;
}
