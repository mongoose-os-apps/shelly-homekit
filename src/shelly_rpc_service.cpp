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

#include "shelly_sw_service.h"

static HAPAccessoryServerRef *s_server;
static HAPPlatformKeyValueStoreRef s_kvs;
static HAPPlatformTCPStreamManagerRef s_tcpm;

static void shelly_get_info_handler(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  const char *ssid = mgos_sys_config_get_wifi_sta_ssid();
  const char *pass = mgos_sys_config_get_wifi_sta_pass();
  bool hap_provisioned = !mgos_conf_str_empty(mgos_sys_config_get_hap_salt());
  bool hap_paired = HAPAccessoryServerIsPaired(s_server);
#ifdef MGOS_CONFIG_HAVE_SW1
  struct shelly_sw_info sw1;
  shelly_sw_get_info(mgos_sys_config_get_sw1_id(), &sw1);
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
  struct shelly_sw_info sw2;
  shelly_sw_get_info(mgos_sys_config_get_sw2_id(), &sw2);
#endif
  HAPPlatformTCPStreamManagerStats tcpm_stats = {};
  HAPPlatformTCPStreamManagerGetStats(s_tcpm, &tcpm_stats);
  mg_rpc_send_responsef(
      ri,
      "{id: %Q, app: %Q, host: %Q, version: %Q, fw_build: %Q, uptime: %d, "
#ifdef MGOS_CONFIG_HAVE_SW1
      "sw1: {id: %d, name: %Q, type: %d, in_mode: %d, initial: %d, "
      "state: %B, auto_off: %B, auto_off_delay: %.3f"
#ifdef SHELLY_HAVE_PM
      ", apower: %.3f, aenergy: %.3f"
#endif
      "},"
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
      "sw2: {id: %d, name: %Q, type: %d, in_mode: %d, initial: %d, "
      "state: %B, auto_off: %B, auto_off_delay: %.3f"
#ifdef SHELLY_HAVE_PM
      ", apower: %.3f, aenergy: %.3f"
#endif
      "},"
#endif
      "wifi_en: %B, wifi_ssid: %Q, wifi_pass: %Q, "
      "hap_provisioned: %B, hap_paired: %B, "
      "hap_ip_conns_pending: %u, hap_ip_conns_active: %u, "
      "hap_ip_conns_max: %u}",
      mgos_sys_config_get_device_id(), MGOS_APP, mgos_dns_sd_get_host_name(),
      mgos_sys_ro_vars_get_fw_version(), mgos_sys_ro_vars_get_fw_id(),
      (int) mgos_uptime(),
#ifdef MGOS_CONFIG_HAVE_SW1
      mgos_sys_config_get_sw1_id(), mgos_sys_config_get_sw1_name(),
      mgos_sys_config_get_sw1_svc_type(), mgos_sys_config_get_sw1_in_mode(),
      mgos_sys_config_get_sw1_initial_state(), sw1.state,
      mgos_sys_config_get_sw1_auto_off(),
      mgos_sys_config_get_sw1_auto_off_delay(),
#ifdef SHELLY_HAVE_PM
      sw1.apower, sw1.aenergy,
#endif
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
      mgos_sys_config_get_sw2_id(), mgos_sys_config_get_sw2_name(),
      mgos_sys_config_get_sw2_svc_type(), mgos_sys_config_get_sw2_in_mode(),
      mgos_sys_config_get_sw2_initial_state(), sw2.state,
      mgos_sys_config_get_sw2_auto_off(),
      mgos_sys_config_get_sw2_auto_off_delay(),
#ifdef SHELLY_HAVE_PM
      sw2.apower, sw2.aenergy,
#endif
#endif
      mgos_sys_config_get_wifi_sta_enable(), (ssid ? ssid : ""),
      (pass ? pass : ""), hap_provisioned, hap_paired,
      (unsigned) tcpm_stats.numPendingTCPStreams,
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

static void shelly_set_config_handler(struct mg_rpc_request_info *ri,
                                      void *cb_arg,
                                      struct mg_rpc_frame_info *fi,
                                      struct mg_str args) {
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

bool shelly_rpc_service_init(HAPAccessoryServerRef *server,
                             HAPPlatformKeyValueStoreRef kvs,
                             HAPPlatformTCPStreamManagerRef tcpm) {
  s_server = server;
  s_kvs = kvs;
  s_tcpm = tcpm;
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.GetInfo", "",
                     shelly_get_info_handler, NULL);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetConfig",
                     "{config: %M, reboot: %B}", shelly_set_config_handler,
                     NULL);
  return true;
}
