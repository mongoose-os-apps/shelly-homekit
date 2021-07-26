/*
 * Copyright (c) Shelly-HomeKit Contributors
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
#include "mgos_http_server.h"
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

static inline bool IsAuthEn() {
  return !mgos_conf_str_empty(mgos_sys_config_get_rpc_auth_file());
}

static void PublishHTTP() {
  std::string txt;
  txt.append("failsafe=");
  txt.append(IsFailsafeMode() ? "1" : "0");
  txt.append(",auth_en=");
  txt.append(IsAuthEn() ? "1" : "0");
  mgos_http_server_publish_dns_sd(txt.c_str());
}

static void GetInfoHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                           struct mg_rpc_frame_info *fi, struct mg_str args) {
  mg_rpc_send_responsef(
      ri,
      "{device_id: %Q, name: %Q, app: %Q, model: %Q, stock_fw_model: %Q, "
      "host: %Q, version: %Q, fw_build: %Q, uptime: %d, failsafe_mode: %B, "
      "auth_en: %B}",
      mgos_sys_config_get_device_id(), mgos_sys_config_get_shelly_name(),
      MGOS_APP, CS_STRINGIFY_MACRO(PRODUCT_MODEL),
      CS_STRINGIFY_MACRO(STOCK_FW_MODEL), mgos_dns_sd_get_host_name(),
      mgos_sys_ro_vars_get_fw_version(), mgos_sys_ro_vars_get_fw_id(),
      (int) mgos_uptime(), (s_server == nullptr) /* failsafe_mode */,
      IsAuthEn());
  (void) cb_arg;
  (void) fi;
  (void) args;
}

static void GetInfoExtHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                              struct mg_rpc_frame_info *fi,
                              struct mg_str args) {
  bool hap_paired = HAPAccessoryServerIsPaired(s_server);
  bool hap_running = (HAPAccessoryServerGetState(s_server) ==
                      kHAPAccessoryServerState_Running);
  HAPPlatformTCPStreamManagerStats tcpm_stats = {};
  HAPPlatformTCPStreamManagerGetStats(s_tcpm, &tcpm_stats);
  uint16_t hap_cn;
  if (HAPAccessoryServerGetCN(s_kvs, &hap_cn) != kHAPError_None) {
    hap_cn = 0;
  }
  bool debug_en = mgos_sys_config_get_file_logger_enable();
  int flags = GetServiceFlags();
#ifdef MGOS_HAVE_WIFI
  const char *wifi_ssid = mgos_sys_config_get_wifi_sta_ssid();
  int wifi_rssi = mgos_wifi_sta_get_rssi();
  char wifi_ip[16] = {};
  struct mgos_net_ip_info ip_info = {};
  if (mgos_net_get_ip_info(MGOS_NET_IF_TYPE_WIFI, MGOS_NET_IF_WIFI_STA,
                           &ip_info)) {
    mgos_net_ip_to_str(&ip_info.ip, wifi_ip);
  }
  const char *wifi_ap_ssid = mgos_sys_config_get_wifi_ap_ssid();
  const char *wifi_ap_ip = mgos_sys_config_get_wifi_ap_ip();
#endif
  std::string res = mgos::JSONPrintStringf(
      "{device_id: %Q, name: %Q, app: %Q, model: %Q, stock_fw_model: %Q, "
      "host: %Q, version: %Q, fw_build: %Q, uptime: %d, failsafe_mode: %B, "
      "auth_en: %B, auth_domain: %Q, "
#ifdef MGOS_HAVE_WIFI
      "wifi_en: %B, wifi_ssid: %Q, wifi_pass: %Q, "
      "wifi_rssi: %d, wifi_ip: %Q, wifi_ap_ssid: %Q, wifi_ap_ip: %Q, "
#endif
      "hap_cn: %d, hap_running: %B, hap_paired: %B, "
      "hap_ip_conns_pending: %u, hap_ip_conns_active: %u, "
      "hap_ip_conns_max: %u, sys_mode: %d, wc_avail: %B, gdo_avail: %B, "
      "debug_en: %B, ota_progress: %d",
      mgos_sys_config_get_device_id(), mgos_sys_config_get_shelly_name(),
      MGOS_APP, CS_STRINGIFY_MACRO(PRODUCT_MODEL),
      CS_STRINGIFY_MACRO(STOCK_FW_MODEL), mgos_dns_sd_get_host_name(),
      mgos_sys_ro_vars_get_fw_version(), mgos_sys_ro_vars_get_fw_id(),
      (int) mgos_uptime(), false /* failsafe_mode */, IsAuthEn(),
      mgos_sys_config_get_rpc_auth_domain(),
#ifdef MGOS_HAVE_WIFI
      mgos_sys_config_get_wifi_sta_enable(), (wifi_ssid ? wifi_ssid : ""),
      (mgos_sys_config_get_wifi_sta_pass() ? "***" : ""), wifi_rssi, wifi_ip,
      (wifi_ap_ssid ? wifi_ap_ssid : ""), (wifi_ap_ip ? wifi_ap_ip : ""),
#endif
      hap_cn, hap_running, hap_paired,
      (unsigned) tcpm_stats.numPendingTCPStreams,
      (unsigned) tcpm_stats.numActiveTCPStreams,
      (unsigned) tcpm_stats.maxNumTCPStreams, mgos_sys_config_get_shelly_mode(),
#ifdef MGOS_SYS_CONFIG_HAVE_WC1  // wc_avail
      true,
#else
      false,
#endif
#ifdef MGOS_SYS_CONFIG_HAVE_GDO1  // gdo_avail
      true,
#else
      false,
#endif
      debug_en, GetOTAProgress());
  auto sys_temp = GetSystemTemperature();
  if (sys_temp.ok()) {
    mgos::JSONAppendStringf(&res, ", sys_temp: %d, overheat_on: %B",
                            sys_temp.ValueOrDie(),
                            (flags & SHELLY_SERVICE_FLAG_OVERHEAT));
  }
  mgos::JSONAppendStringf(&res, ", components: [");
  bool first = true;
  for (const auto &c : g_comps) {
    const auto &is = c->GetInfoJSON();
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
    char *name_c = nullptr;
    int sys_mode = -1;
    int8_t debug_en = -1;
    json_scanf(config_tok.ptr, config_tok.len,
               "{name: %Q, sys_mode: %d, debug_en: %B}", &name_c, &sys_mode,
               &debug_en);
    mgos::ScopedCPtr name_owner(name_c);

    if (sys_mode >= 0 && sys_mode <= 4) {
      if (sys_mode != mgos_sys_config_get_shelly_mode()) {
        mgos_sys_config_set_shelly_mode(sys_mode);
        restart_required = true;
      }
    } else if (sys_mode == -1) {
      // Nothing.
    } else {
      st = mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "sys_mode");
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
      if (strcmp(mgos_sys_config_get_shelly_name(), name.c_str()) != 0) {
        LOG(LL_INFO, ("Name change: %s -> %s",
                      mgos_sys_config_get_shelly_name(), name.c_str()));
        mgos_sys_config_set_shelly_name(name.c_str());
        mgos_sys_config_set_dns_sd_host_name(name.c_str());
        mgos_dns_sd_set_host_name(name.c_str());
        PublishHTTP();
        restart_required = true;
      }
    }
    if (debug_en != -1) {
      SetDebugEnable(debug_en);
    }
  } else {
    // Component settings.
    bool found = false;
    for (auto &c : g_comps) {
      if (c->id() != id || (int) c->type() != type) continue;
      st = c->SetConfig(std::string(config_tok.ptr, config_tok.len),
                        &restart_required);
      found = true;
      break;
    }
    if (!found) {
      st = mgos::Errorf(STATUS_INVALID_ARGUMENT, "component not found");
    }
  }
  if (st.ok()) {
    LOG(LL_ERROR, ("SetConfig ok, %d", restart_required));
    mgos_sys_config_save(&mgos_sys_config, false /* try once */, nullptr);
    if (restart_required) {
      LOG(LL_INFO, ("Configuration change requires server restart"));
      RestartService();
    }
  }
  SendStatusResp(ri, st);

  (void) cb_arg;
  (void) fi;
}

static void SetStateHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                            struct mg_rpc_frame_info *fi, struct mg_str args) {
  int id = -1;
  int type = -1;
  struct json_token state_tok = JSON_INVALID_TOKEN;

  json_scanf(args.p, args.len, ri->args_fmt, &id, &type, &state_tok);

  if (state_tok.len == 0) {
    mg_rpc_send_errorf(ri, 400, "%s is required", "state");
    return;
  }

  Status st = Status::OK();
  bool found = false;
  for (auto &c : g_comps) {
    if (c->id() != id || (int) c->type() != type) continue;
    st = c->SetState(std::string(state_tok.ptr, state_tok.len));
    found = true;
    break;
  }
  if (!found) {
    st = mgos::Errorf(STATUS_INVALID_ARGUMENT, "component not found");
  }
  SendStatusResp(ri, st);

  (void) cb_arg;
  (void) fi;
}

static void InjectInputEventHandler(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  int id = -1, ev = -1;

  json_scanf(args.p, args.len, ri->args_fmt, &id, &ev);

  if (id < 0 || ev < 0) {
    mg_rpc_send_errorf(ri, 400, "%s are required", "id and event");
    return;
  }
  // Should we allow Reset event to be injected? Maybe. Disallow for now.
  // For now we only allow "higher-level" events to be injected,
  // since injecting Change won't match with the value returne by GetState.
  if (ev != (int) Input::Event::kSingle && ev != (int) Input::Event::kDouble &&
      ev != (int) Input::Event::kLong) {
    mg_rpc_send_errorf(ri, 400, "invalid %s", "event");
    return;
  }

  Input *in = FindInput(id);

  if (in == nullptr) {
    mg_rpc_send_errorf(ri, 400, "%s not found", "input");
    return;
  }

  in->InjectEvent(static_cast<Input::Event>(ev), false);

  mg_rpc_send_responsef(ri, nullptr);

  (void) cb_arg;
  (void) fi;
}

static void GetDebugInfoHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                                struct mg_rpc_frame_info *fi,
                                struct mg_str args) {
  std::string res;
  GetDebugInfo(&res);
  mg_rpc_send_responsef(ri, "{info: %Q}", res.c_str());
  (void) cb_arg;
  (void) args;
  (void) fi;
}

static void WipeDeviceHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                              struct mg_rpc_frame_info *fi,
                              struct mg_str args) {
  bool wiped = WipeDevice();
  mg_rpc_send_responsef(ri, "{wiped: %B}", wiped);
  if (wiped) {
    mgos_system_restart_after(500);
  }
  (void) cb_arg;
  (void) args;
  (void) fi;
}

static void AbortHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                         struct mg_rpc_frame_info *fi, struct mg_str args) {
  LOG(LL_ERROR, ("Aborting as requested"));
  abort();
  (void) cb_arg;
  (void) args;
  (void) fi;
  (void) ri;
}

static Status SetAuthFileName(const std::string &passwd_fname,
                              const std::string &auth_domain,
                              const std::string &acl_fname) {
  mgos_sys_config_set_http_auth_file(passwd_fname.c_str());
  mgos_sys_config_set_http_auth_domain(auth_domain.c_str());
  mgos_sys_config_set_rpc_auth_file(passwd_fname.c_str());
  mgos_sys_config_set_rpc_auth_domain(auth_domain.c_str());
  mgos_sys_config_set_rpc_acl_file(acl_fname.c_str());
  char *err = nullptr;
  if (!mgos_sys_config_save(&mgos_sys_config, false /* try once */, &err)) {
    return mgos::Errorf(STATUS_UNAVAILABLE, "Failed to save config: %s", err);
  }
  struct mg_http_endpoint *ep =
      mg_get_http_endpoints(mgos_get_sys_http_server());
  for (; ep != NULL; ep = ep->next) {
    // No auth for root and /rpc (auth handled by RPC itself).
    if (mg_vcmp(&ep->uri_pattern, "/") == 0 ||
        mg_vcmp(&ep->uri_pattern, MGOS_RPC_HTTP_URI_PREFIX) == 0) {
      continue;
    }
    free(ep->auth_file);
    free(ep->auth_domain);
    if (passwd_fname.empty()) {
      ep->auth_file = nullptr;
      ep->auth_domain = nullptr;
    } else {
      ep->auth_file = strdup(passwd_fname.c_str());
      ep->auth_domain = strdup(auth_domain.c_str());
      ep->auth_algo = (enum mg_auth_algo) mgos_sys_config_get_http_auth_algo();
    }
  }
  PublishHTTP();  // Re-publish the HTTP service to update auth_en.
  return Status::OK();
}

static void SetAuthHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                           struct mg_rpc_frame_info *fi, struct mg_str args) {
  char *user = nullptr, *realm = nullptr, *ha1 = nullptr;
  json_scanf(args.p, args.len, ri->args_fmt, &user, &realm, &ha1);
  mgos::ScopedCPtr user_owner(user), realm_owner(realm), ha1_owner(ha1);

  if (user == nullptr) {
    mg_rpc_send_errorf(ri, 400, "%s is required", "user");
    return;
  }
  if (realm == nullptr) {
    mg_rpc_send_errorf(ri, 400, "%s is required", "realm");
    return;
  }
  if (ha1 == nullptr) {
    mg_rpc_send_errorf(ri, 400, "%s is required", "ha1");
    return;
  }
  // Must be AUTH_USER or ACLs won't work.
  if (std::string(user) != AUTH_USER) {
    mg_rpc_send_errorf(ri, 400, "incorrect %s", "user");
    return;
  }

  size_t l = std::string(ha1).length();
  if (l == 0) {
    auto st = SetAuthFileName("", "", "");
    if (st.ok()) remove(AUTH_FILE_NAME);
    SendStatusResp(ri, st);
    return;
  } else if (l != 64) {
    mg_rpc_send_errorf(ri, 400, "invalid %s", "ha1");
    return;
  }

  FILE *fp = fopen(AUTH_FILE_NAME, "w");
  if (fp == nullptr) {
    mg_rpc_send_errorf(ri, 500, "failed to %s", "save file");
    return;
  }
  fprintf(fp, "%s:%s:%s\n", AUTH_USER, realm, ha1);
  fclose(fp);

  auto st = SetAuthFileName(AUTH_FILE_NAME, realm, ACL_FILE_NAME);
  SendStatusResp(ri, st);

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
  if (server != nullptr) {
    mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.GetInfoExt", "",
                       GetInfoExtHandler, nullptr);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetConfig",
                       "{id: %d, type: %d, config: %T}", SetConfigHandler,
                       nullptr);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetState",
                       "{id: %d, type: %d, state: %T}", SetStateHandler,
                       nullptr);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.InjectInputEvent",
                       "{id: %d, event: %d}", InjectInputEventHandler, nullptr);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.Abort", "", AbortHandler,
                       nullptr);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetAuth",
                       "{user: %Q, realm: %Q, ha1: %Q}", SetAuthHandler,
                       nullptr);
  }
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.GetDebugInfo", "",
                     GetDebugInfoHandler, nullptr);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.WipeDevice", "",
                     WipeDeviceHandler, nullptr);
  PublishHTTP();  // Update TXT records for the HTTP service.
  return true;
}

}  // namespace shelly
