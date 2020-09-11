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

#include "shelly_rpc_service.h"

#include "mgos.h"
#include "mgos_dns_sd.h"
#include "mgos_rpc.h"

#include "HAPAccessoryServer+Internal.h"

#include "shelly_debug.h"
#include "shelly_main.h"

namespace shelly {

static HAPAccessoryServerRef *s_server;
static HAPPlatformKeyValueStoreRef s_kvs;
static HAPPlatformTCPStreamManagerRef s_tcpm;

static void GetInfoHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                           struct mg_rpc_frame_info *fi, struct mg_str args) {
#ifdef MGOS_HAVE_WIFI
  const char *ssid = mgos_sys_config_get_wifi_sta_ssid();
  const char *pass = mgos_sys_config_get_wifi_sta_pass();
#endif
  bool hap_provisioned = !mgos_conf_str_empty(mgos_sys_config_get_hap_salt());
  bool hap_paired = HAPAccessoryServerIsPaired(s_server);
#ifdef MGOS_CONFIG_HAVE_SW1
  auto sw1_info = g_sw1->GetInfo();
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
  auto sw2_info = g_sw2->GetInfo();
#endif
  HAPPlatformTCPStreamManagerStats tcpm_stats = {};
  HAPPlatformTCPStreamManagerGetStats(s_tcpm, &tcpm_stats);
  mg_rpc_send_responsef(
      ri,
      "{id: %Q, app: %Q, model: %Q, host: %Q, "
      "version: %Q, fw_build: %Q, uptime: %d, "
#ifdef MGOS_CONFIG_HAVE_SW1
      "sw1: %s,"
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
      "sw2: %s,"
#endif
#ifdef MGOS_HAVE_WIFI
      "wifi_en: %B, wifi_ssid: %Q, wifi_pass: %Q, "
#endif
      "hap_provisioned: %B, hap_paired: %B, "
      "hap_ip_conns_pending: %u, hap_ip_conns_active: %u, "
      "hap_ip_conns_max: %u}",
      mgos_sys_config_get_device_id(), MGOS_APP,
      CS_STRINGIFY_MACRO(PRODUCT_MODEL), mgos_dns_sd_get_host_name(),
      mgos_sys_ro_vars_get_fw_version(), mgos_sys_ro_vars_get_fw_id(),
      (int) mgos_uptime(),
#ifdef MGOS_CONFIG_HAVE_SW1
      (sw1_info.ok() ? sw1_info.ValueOrDie().c_str() : ""),
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
      (sw2_info.ok() ? sw2_info.ValueOrDie().c_str() : ""),
#endif
#ifdef MGOS_HAVE_WIFI
      mgos_sys_config_get_wifi_sta_enable(), (ssid ? ssid : ""),
      (pass ? pass : ""),
#endif
      hap_provisioned, hap_paired, (unsigned) tcpm_stats.numPendingTCPStreams,
      (unsigned) tcpm_stats.numActiveTCPStreams,
      (unsigned) tcpm_stats.maxNumTCPStreams);
  (void) cb_arg;
  (void) fi;
  (void) args;
}

static void set_handler(const char *str, int len, void *arg) {
  char *acl_copy = (mgos_sys_config_get_conf_acl() != NULL
                        ? strdup(mgos_sys_config_get_conf_acl())
                        : NULL);
  mgos_conf_parse(mg_mk_str_n(str, len), acl_copy, mgos_config_schema(),
                  &mgos_sys_config);
  free(acl_copy);
  (void) arg;
}

static void SetConfigHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                             struct mg_rpc_frame_info *fi, struct mg_str args) {
  const char *msg = "";
#ifdef MGOS_CONFIG_HAVE_SW1
  int old_sw1_svc_type = mgos_sys_config_get_sw1_svc_type();
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
  int old_sw2_svc_type = mgos_sys_config_get_sw2_svc_type();
#endif
  bool reboot = false;

  json_scanf(args.p, args.len, ri->args_fmt, set_handler, NULL, &reboot);

#ifdef MGOS_CONFIG_HAVE_SW1
  if (mgos_sys_config_get_sw1_svc_type() != old_sw1_svc_type) {
    if (HAPAccessoryServerIncrementCN(s_kvs) == kHAPError_None) {
      msg = "Please reboot the device for changes to take effect";
    }
  }
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
  if (mgos_sys_config_get_sw2_svc_type() != old_sw2_svc_type) {
    if (HAPAccessoryServerIncrementCN(s_kvs) == kHAPError_None) {
      msg = "Please reboot the device for changes to take effect";
    }
  }
#endif

  mgos_sys_config_save(&mgos_sys_config, false /* try once */, NULL);

  mg_rpc_send_responsef(ri, "{msg: %Q}", msg);

  if (reboot) mgos_system_restart_after(300);

  (void) cb_arg;
  (void) fi;
}

static void GetDebugInfoHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                                struct mg_rpc_frame_info *fi,
                                struct mg_str args) {
  std::string res;
  shelly_get_debug_info(&res);
  mg_rpc_send_responsef(ri, "{info: %Q}", res.c_str());
  (void) cb_arg;
  (void) args;
  (void) fi;
}

static void SetSwitchHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                             struct mg_rpc_frame_info *fi, struct mg_str args) {
  int id = -1;
  bool state = false;

  json_scanf(args.p, args.len, ri->args_fmt, &id, &state);

  if (g_sw1 != nullptr && id == g_sw1->id()) {
    g_sw1->SetState(state, "web");
    mg_rpc_send_responsef(ri, NULL);
  } else if (g_sw2 != nullptr && id == g_sw2->id()) {
    g_sw2->SetState(state, "web");
    mg_rpc_send_responsef(ri, NULL);
  } else {
    mg_rpc_send_errorf(ri, 400, "bad args");
  }

  (void) cb_arg;
  (void) fi;
}

bool shelly_rpc_service_init(HAPAccessoryServerRef *server,
                             HAPPlatformKeyValueStoreRef kvs,
                             HAPPlatformTCPStreamManagerRef tcpm) {
  s_server = server;
  s_kvs = kvs;
  s_tcpm = tcpm;
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.GetInfo", "",
                     GetInfoHandler, NULL);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetConfig",
                     "{config: %M, reboot: %B}", SetConfigHandler, NULL);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.GetDebugInfo", "",
                     GetDebugInfoHandler, NULL);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetSwitch",
                     "{id: %d, state: %B}", SetSwitchHandler, NULL);
  return true;
}

}  // namespace shelly
