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
#include "shelly_hap_switch.h"
#include "shelly_main.h"

namespace shelly {

static HAPAccessoryServerRef *s_server;
static HAPPlatformKeyValueStoreRef s_kvs;
static HAPPlatformTCPStreamManagerRef s_tcpm;

static void SendStatusResp(struct mg_rpc_request_info *ri, const Status &st) {
  if (st.ok()) {
    mg_rpc_send_responsef(ri, nullptr);
    return;
  }
  mg_rpc_send_errorf(ri, st.error_code(), "%s", st.error_message().c_str());
}

static void GetInfoHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                           struct mg_rpc_frame_info *fi, struct mg_str args) {
#ifdef MGOS_HAVE_WIFI
  const char *ssid = mgos_sys_config_get_wifi_sta_ssid();
  const char *pass = mgos_sys_config_get_wifi_sta_pass();
#endif
  bool hap_provisioned = !mgos_conf_str_empty(mgos_sys_config_get_hap_salt());
  bool hap_paired = HAPAccessoryServerIsPaired(s_server);
  HAPPlatformTCPStreamManagerStats tcpm_stats = {};
  HAPPlatformTCPStreamManagerGetStats(s_tcpm, &tcpm_stats);
  uint16_t hap_cn;
  if (HAPAccessoryServerGetCN(s_kvs, &hap_cn) != kHAPError_None) {
    hap_cn = 0;
  }
  std::string res = mgos::JSONPrintStringf(
      "{id: %Q, app: %Q, model: %Q, host: %Q, "
      "version: %Q, fw_build: %Q, uptime: %d, "
#ifdef MGOS_HAVE_WIFI
      "wifi_en: %B, wifi_ssid: %Q, wifi_pass: %Q, "
#endif
      "hap_cn: %d, hap_provisioned: %B, hap_paired: %B, "
      "hap_ip_conns_pending: %u, hap_ip_conns_active: %u, "
      "hap_ip_conns_max: %u",
      mgos_sys_config_get_device_id(), MGOS_APP,
      CS_STRINGIFY_MACRO(PRODUCT_MODEL), mgos_dns_sd_get_host_name(),
      mgos_sys_ro_vars_get_fw_version(), mgos_sys_ro_vars_get_fw_id(),
      (int) mgos_uptime(),
#ifdef MGOS_HAVE_WIFI
      mgos_sys_config_get_wifi_sta_enable(), (ssid ? ssid : ""),
      (pass ? pass : ""),
#endif
      hap_cn, hap_provisioned, hap_paired,
      (unsigned) tcpm_stats.numPendingTCPStreams,
      (unsigned) tcpm_stats.numActiveTCPStreams,
      (unsigned) tcpm_stats.maxNumTCPStreams);
  mgos::JSONAppendStringf(&res, ", components: [");
  bool first = true;
  for (const auto &c : g_components) {
    const auto &is = c->GetInfo();
    if (is.ok()) {
      if (!first) res.append(", ");
      res.append(is.ValueOrDie());
      first = false;
    }
  }

  mgos::JSONAppendStringf(&res, "]}");

  mg_rpc_send_responsef(ri, "%s", res.c_str());

  (void) cb_arg;
  (void) fi;
  (void) args;
}

static void SetConfigHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                             struct mg_rpc_frame_info *fi, struct mg_str args) {
  int id = -1;
  int type = -1;
  struct json_token config_tok = JSON_INVALID_TOKEN;

  json_scanf(args.p, args.len, ri->args_fmt, &id, &type, &config_tok);

  if (config_tok.len == 0) {
    mg_rpc_send_errorf(ri, 400, "%s is required", "config");
    return;
  }

  Component *c = nullptr;
  for (auto &cp : g_components) {
    if (cp->id() == id && (int) cp->type() == type) {
      c = cp.get();
      break;
    }
  }
  if (c == nullptr) {
    mg_rpc_send_errorf(ri, 400, "component not found");
    return;
  }

  bool restart_required = false;
  auto st = c->SetConfig(std::string(config_tok.ptr, config_tok.len),
                         &restart_required);
  if (st.ok()) {
    mgos_sys_config_save(&mgos_sys_config, false /* try once */, NULL);
    if (restart_required) {
      LOG(LL_INFO, ("Configuration change requires server restart"));
      RestartHAPServer();
    }
    mg_rpc_send_responsef(ri, nullptr);
  } else {
    SendStatusResp(ri, st);
  }

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

  for (auto &c : g_components) {
    if (c->id() != id) continue;
    switch (c->type()) {
      case Component::Type::kSwitch:
      case Component::Type::kOutlet:
      case Component::Type::kLock: {
        ShellySwitch *sw = static_cast<ShellySwitch *>(c.get());
        sw->SetState(state, "web");
        mg_rpc_send_responsef(ri, NULL);
        return;
      }
      default:
        break;
    }
  }
  mg_rpc_send_errorf(ri, 400, "component not found");

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
                     "{id: %d, type: %d, config: %T}", SetConfigHandler, NULL);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.GetDebugInfo", "",
                     GetDebugInfoHandler, NULL);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetSwitch",
                     "{id: %d, state: %B}", SetSwitchHandler, NULL);
  return true;
}

}  // namespace shelly
