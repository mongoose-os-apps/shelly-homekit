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

#include "shelly_rpc_service.hpp"

#include "mgos.hpp"
#include "mgos_dns_sd.h"
#include "mgos_rpc.h"

#include "HAPAccessoryServer+Internal.h"

#include "shelly_debug.hpp"
#include "shelly_hap_switch.hpp"
#include "shelly_main.hpp"

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
  bool hap_paired = HAPAccessoryServerIsPaired(s_server);
  bool hap_running = (HAPAccessoryServerGetState(s_server) ==
                      kHAPAccessoryServerState_Running);
  HAPPlatformTCPStreamManagerStats tcpm_stats = {};
  HAPPlatformTCPStreamManagerGetStats(s_tcpm, &tcpm_stats);
  uint16_t hap_cn;
  if (HAPAccessoryServerGetCN(s_kvs, &hap_cn) != kHAPError_None) {
    hap_cn = 0;
  }
  std::string res = mgos::JSONPrintStringf(
      "{id: %Q, app: %Q, model: %Q, stock_model: %Q, host: %Q, "
      "version: %Q, fw_build: %Q, uptime: %d, "
#ifdef MGOS_HAVE_WIFI
      "wifi_en: %B, wifi_ssid: %Q, wifi_pass: %Q, "
#endif
      "hap_cn: %d, hap_running: %B, hap_paired: %B, "
      "hap_ip_conns_pending: %u, hap_ip_conns_active: %u, "
      "hap_ip_conns_max: %u, sys_mode: %d, rsh_avail: %B",
      mgos_sys_config_get_device_id(), MGOS_APP,
      CS_STRINGIFY_MACRO(PRODUCT_MODEL), CS_STRINGIFY_MACRO(STOCK_FW_MODEL),
      mgos_dns_sd_get_host_name(), mgos_sys_ro_vars_get_fw_version(),
      mgos_sys_ro_vars_get_fw_id(), (int) mgos_uptime(),
#ifdef MGOS_HAVE_WIFI
      mgos_sys_config_get_wifi_sta_enable(), (ssid ? ssid : ""),
      (pass ? "***" : ""),
#endif
      hap_cn, hap_running, hap_paired,
      (unsigned) tcpm_stats.numPendingTCPStreams,
      (unsigned) tcpm_stats.numActiveTCPStreams,
      (unsigned) tcpm_stats.maxNumTCPStreams, mgos_sys_config_get_shelly_mode(),
#ifdef MGOS_SYS_CONFIG_HAVE_WC1
      true
#else
      false
#endif
  );
  mgos::JSONAppendStringf(&res, ", components: [");
  bool first = true;
  for (const auto *c : g_comps) {
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

  Status st = Status::OK();
  bool restart_required = false;
  if (id == -1 && type == -1) {
    // System settings.
    char *name_c = NULL;
    int sys_mode = -1;
    json_scanf(config_tok.ptr, config_tok.len, "{name: %Q, sys_mode: %d}",
               &name_c, &sys_mode);
    mgos::ScopedCPtr name_owner(name_c);

    if (sys_mode == 0 || sys_mode == 1) {
      if (sys_mode != mgos_sys_config_get_shelly_mode()) {
        mgos_sys_config_set_shelly_mode(sys_mode);
        restart_required = true;
      }
    } else if (sys_mode == -1) {
      // Nothing.
    } else {
      st = mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
    }
    if (name_c != nullptr) {
      mgos_expand_mac_address_placeholders(name_c);
      std::string name(name_c);
      if (name.length() > 64) {
        mg_rpc_send_errorf(ri, 400, "invalid %s", "name");
        return;
      }
      for (char c : name) {
        if (!std::isalnum(c) && c != '-') {
          mg_rpc_send_errorf(ri, 400, "invalid %s", "name");
          return;
        }
      }
      mgos_sys_config_set_device_id(name.c_str());
      mgos_sys_config_set_dns_sd_host_name(nullptr);
      mgos_dns_sd_set_host_name(name.c_str());
      restart_required = true;
    }
  } else {
    // Component settings.
    Component *c = nullptr;
    for (auto *cp : g_comps) {
      if (cp->id() == id && (int) cp->type() == type) {
        c = cp;
        break;
      }
    }
    if (c == nullptr) {
      st = mgos::Errorf(STATUS_INVALID_ARGUMENT, "component not found");
      return;
    } else {
      st = c->SetConfig(std::string(config_tok.ptr, config_tok.len),
                        &restart_required);
    }
  }
  if (st.ok()) {
    mgos_sys_config_save(&mgos_sys_config, false /* try once */, nullptr);
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

  for (auto *c : g_comps) {
    if (c->id() != id) continue;
    switch (c->type()) {
      case Component::Type::kSwitch:
      case Component::Type::kOutlet:
      case Component::Type::kLock: {
        ShellySwitch *sw = static_cast<ShellySwitch *>(c);
        sw->SetState(state, "web");
        mg_rpc_send_responsef(ri, nullptr);
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
                     GetInfoHandler, nullptr);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetConfig",
                     "{id: %d, type: %d, config: %T}", SetConfigHandler,
                     nullptr);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.GetDebugInfo", "",
                     GetDebugInfoHandler, nullptr);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetSwitch",
                     "{id: %d, state: %B}", SetSwitchHandler, nullptr);
  return true;
}

}  // namespace shelly
