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

#include "mbedtls/sha256.h"

#include "shelly_debug.hpp"
#include "shelly_hap_switch.hpp"
#include "shelly_main.hpp"
#include "shelly_ota.hpp"
#include "shelly_wifi_config.hpp"

namespace shelly {

static HAPAccessoryServerRef *s_server;
static HAPPlatformKeyValueStoreRef s_kvs;
static HAPPlatformTCPStreamManagerRef s_tcpm;

void SendStatusResp(struct mg_rpc_request_info *ri, const Status &st) {
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

void ReportRPCRequest(struct mg_rpc_request_info *ri) {
  char *info = ri->ch->get_info(ri->ch);
  if (info != nullptr) {
    ReportClientRequest(info);
    free(info);
  }
}

static void GetInfoHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                           struct mg_rpc_frame_info *fi, struct mg_str args) {
  ReportRPCRequest(ri);
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

static void AppendBasicInfoExt(std::string *res) {
  const char *device_id = mgos_sys_config_get_device_id();
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
  mgos::JSONAppendStringf(
      res,
      "device_id: %Q, name: %Q, app: %Q, model: %Q, stock_fw_model: %Q, "
      "host: %Q, version: %Q, fw_build: %Q, uptime: %d, failsafe_mode: %B, "
      "auth_en: %B, auth_domain: %Q, "
      "hap_cn: %d, hap_running: %B, hap_paired: %B, "
      "hap_ip_conns_pending: %u, hap_ip_conns_active: %u, "
      "hap_ip_conns_max: %u, sys_mode: %d, wc_avail: %B, gdo_avail: %B, "
      "debug_en: %B, ",
      device_id, mgos_sys_config_get_shelly_name(), MGOS_APP,
      CS_STRINGIFY_MACRO(PRODUCT_MODEL), CS_STRINGIFY_MACRO(STOCK_FW_MODEL),
      mgos_dns_sd_get_host_name(), mgos_sys_ro_vars_get_fw_version(),
      mgos_sys_ro_vars_get_fw_id(), (int) mgos_uptime(),
      false /* failsafe_mode */, IsAuthEn(),
      mgos_sys_config_get_rpc_auth_domain(), hap_cn, hap_running, hap_paired,
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
      debug_en);
  auto sys_temp = GetSystemTemperature();
  if (sys_temp.ok()) {
    mgos::JSONAppendStringf(res, "sys_temp: %d, overheat_on: %B, ",
                            sys_temp.ValueOrDie(),
                            (flags & SHELLY_SERVICE_FLAG_OVERHEAT));
  }
}

static void AppendWifiInfoExt(std::string *res) {
  const char *device_id = mgos_sys_config_get_device_id();
  WifiConfig wc = GetWifiConfig();
  WifiInfo wi = GetWifiInfo();
  /* Do not return plaintext password, mix it up with SSID and device ID. */
  uint32_t digest[8];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0 /* is224 */);
  mbedtls_sha256_update(&ctx, (uint8_t *) device_id, strlen(device_id));
  mbedtls_sha256_update(&ctx, (uint8_t *) wc.sta.ssid.data(),
                        wc.sta.ssid.length());
  mbedtls_sha256_update(&ctx, (uint8_t *) wc.sta.pass.data(),
                        wc.sta.pass.length());
  mbedtls_sha256_finish(&ctx, (uint8_t *) digest);
  mbedtls_sha256_free(&ctx);
  std::string wifi_pass = ScreenPassword(wc.sta.pass);
  std::string wifi1_pass = ScreenPassword(wc.sta1.pass);
  std::string wifi_ap_pass = ScreenPassword(wc.ap.pass);
  mgos::JSONAppendStringf(
      res,
      "wifi_en: %B, wifi_ssid: %Q, wifi_pass: %Q, "
      "wifi_pass_h: \"%08x%08x%08x%08x\", "
      "wifi_ip: %Q, wifi_netmask: %Q, wifi_gw: %Q, wifi_nameserver: %Q, "
      "wifi1_en: %B, wifi1_ssid: %Q, wifi1_pass: %Q, "
      "wifi1_ip: %Q, wifi1_netmask: %Q, wifi1_gw: %Q, wifi1_nameserver: %Q, "
      "wifi_ap_en: %B, wifi_ap_ssid: %Q, wifi_ap_pass: %Q, "
      "wifi_connecting: %B, wifi_connected: %B, wifi_conn_ssid: %Q, "
      "wifi_conn_rssi: %d, wifi_conn_ip: %Q, "
      "wifi_status: %Q, wifi_sta_ps_mode: %d, mac_address: %Q, ",
      wc.sta.enable, wc.sta.ssid.c_str(), wifi_pass.c_str(),
      (unsigned int) digest[0], (unsigned int) digest[2],
      (unsigned int) digest[4], (unsigned int) digest[6], wc.sta.ip.c_str(),
      wc.sta.netmask.c_str(), wc.sta.gw.c_str(), wc.sta.nameserver.c_str(),
      wc.sta1.enable, wc.sta1.ssid.c_str(), wifi1_pass.c_str(),
      wc.sta1.ip.c_str(), wc.sta1.netmask.c_str(), wc.sta1.gw.c_str(),
      wc.sta1.nameserver.c_str(), wc.ap.enable, wc.ap.ssid.c_str(),
      wifi_ap_pass.c_str(), wi.sta_connecting, wi.sta_connected,
      wi.sta_ssid.c_str(), wi.sta_rssi, wi.sta_ip.c_str(), wi.status.c_str(),
      wc.sta_ps_mode, GetMACAddr(true /* sta */, true /* delims */).c_str());
}

static void AppendOTAInfoExt(std::string *res) {
  OTAProgress otap;
  auto otaps = GetOTAProgress();
  if (otaps.ok()) otap = otaps.ValueOrDie();
  mgos::JSONAppendStringf(
      res, "ota_progress: %d, ota_version: %Q, ota_build: %Q, ",
      otap.progress_pct, otap.version.c_str(), otap.build.c_str());
}

static void AppendCompoentInfoExt(std::string *res) {
  mgos::JSONAppendStringf(res, "components: [");
  bool first = true;
  for (const auto &c : g_comps) {
    const auto &is = c->GetInfoJSON();
    if (is.ok()) {
      if (!first) res->append(", ");
      res->append(is.ValueOrDie());
      first = false;
    }
  }
  res->append("]");
}

static void GetInfoExtHandler(struct mg_rpc_request_info *ri,
                              void *cb_arg UNUSED_ARG,
                              struct mg_rpc_frame_info *fi UNUSED_ARG,
                              struct mg_str args UNUSED_ARG) {
  std::string res;
  AppendBasicInfoExt(&res);
  AppendWifiInfoExt(&res);
  AppendOTAInfoExt(&res);
  AppendCompoentInfoExt(&res);
  ReportRPCRequest(ri);
  mg_rpc_send_responsef(ri, "{%s}", res.c_str());
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

    if (sys_mode >= (int) Mode::kDefault && sys_mode < (int) Mode::kMax) {
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
      LOG(LL_INFO, ("Configuration change requires %s", "server restart"));
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

static void IdentifyHandler(struct mg_rpc_request_info *ri,
                            void *cb_arg UNUSED_ARG,
                            struct mg_rpc_frame_info *fi UNUSED_ARG,
                            struct mg_str args) {
  int id = -1;
  int type = -1;

  json_scanf(args.p, args.len, ri->args_fmt, &id, &type);

  Status st = Status::OK();
  bool found = false;
  for (auto &c : g_comps) {
    if (c->id() != id || (int) c->type() != type) continue;
    c->Identify();
    found = true;
    break;
  }
  if (!found) {
    st = mgos::Errorf(STATUS_INVALID_ARGUMENT, "component not found");
  }
  SendStatusResp(ri, st);
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
                              const std::string &auth_domain, bool acl_en) {
  mgos_sys_config_set_http_auth_file(passwd_fname.c_str());
  mgos_sys_config_set_http_auth_domain(auth_domain.c_str());
  mgos_sys_config_set_rpc_auth_file(passwd_fname.c_str());
  mgos_sys_config_set_rpc_auth_domain(auth_domain.c_str());
  mgos_sys_config_set_rpc_acl(
      acl_en ? mgos_sys_config_get_default__const_rpc_acl() : nullptr);
  mgos_sys_config_set_rpc_acl_file(nullptr);
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
    auto st = SetAuthFileName("", "", false);
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

  auto st = SetAuthFileName(AUTH_FILE_NAME, realm, true);
  SendStatusResp(ri, st);

  (void) cb_arg;
  (void) fi;
}

static void GetWifiConfigHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                                 struct mg_rpc_frame_info *fi,
                                 struct mg_str args) {
  std::string cfg_json = GetWifiConfig().ToJSON();
  mg_rpc_send_responsef(ri, "%s", cfg_json.c_str());
  (void) cb_arg;
  (void) fi;
  (void) args;
}

static void SetWifiConfigHandler(struct mg_rpc_request_info *ri,
                                 void *cb_arg UNUSED_ARG,
                                 struct mg_rpc_frame_info *fi UNUSED_ARG,
                                 struct mg_str args) {
  ReportRPCRequest(ri);
  WifiConfig cfg = GetWifiConfig();
  int8_t ap_enable = -1, sta_enable = -1, sta1_enable = -1;
  char *ap_ssid = nullptr, *ap_pass = nullptr;
  char *sta_ssid = nullptr, *sta_pass = nullptr, *sta_ip = nullptr;
  char *sta_netmask = nullptr, *sta_gw = nullptr, *sta_nameserver = nullptr;
  char *sta1_ssid = nullptr, *sta1_pass = nullptr, *sta1_ip = nullptr;
  char *sta1_netmask = nullptr, *sta1_gw = nullptr, *sta1_nameserver = nullptr;
  json_scanf(args.p, args.len, ri->args_fmt, &ap_enable, &ap_ssid, &ap_pass,
             &sta_enable, &sta_ssid, &sta_pass, &sta_ip, &sta_netmask, &sta_gw,
             &sta_nameserver, &sta1_enable, &sta1_ssid, &sta1_pass, &sta1_ip,
             &sta1_netmask, &sta1_gw, &sta1_nameserver, &cfg.sta_ps_mode);
  mgos::ScopedCPtr o1(ap_ssid), o2(ap_pass);
  mgos::ScopedCPtr o3(sta_ssid), o4(sta_pass), o5(sta_ip);
  mgos::ScopedCPtr o6(sta_netmask), o7(sta_gw), o8(sta_nameserver);
  mgos::ScopedCPtr o9(sta1_ssid), o10(sta1_pass), o11(sta1_ip);
  mgos::ScopedCPtr o12(sta1_netmask), o13(sta1_gw), o14(sta1_nameserver);

  if (ap_enable != -1) cfg.ap.enable = ap_enable;
  if (ap_ssid != nullptr) cfg.ap.ssid = ap_ssid;
  if (ap_pass != nullptr) cfg.ap.pass = ap_pass;

  if (sta_enable != -1) cfg.sta.enable = sta_enable;
  if (sta_ssid != nullptr) cfg.sta.ssid = sta_ssid;
  if (sta_pass != nullptr) cfg.sta.pass = sta_pass;
  if (sta_ip != nullptr) cfg.sta.ip = sta_ip;
  if (sta_netmask != nullptr) cfg.sta.netmask = sta_netmask;
  if (sta_gw != nullptr) cfg.sta.gw = sta_gw;
  if (sta_nameserver != nullptr) cfg.sta.nameserver = sta_nameserver;

  if (sta1_enable != -1) cfg.sta1.enable = sta1_enable;
  if (sta1_ssid != nullptr) cfg.sta1.ssid = sta1_ssid;
  if (sta1_pass != nullptr) cfg.sta1.pass = sta1_pass;
  if (sta1_ip != nullptr) cfg.sta1.ip = sta1_ip;
  if (sta1_netmask != nullptr) cfg.sta1.netmask = sta1_netmask;
  if (sta1_gw != nullptr) cfg.sta1.gw = sta1_gw;
  if (sta1_nameserver != nullptr) cfg.sta1.nameserver = sta1_nameserver;

  Status st = SetWifiConfig(cfg);
  SendStatusResp(ri, st);
}

bool RPCServiceInit(HAPAccessoryServerRef *server,
                    HAPPlatformKeyValueStoreRef kvs,
                    HAPPlatformTCPStreamManagerRef tcpm) {
  s_server = server;
  s_kvs = kvs;
  s_tcpm = tcpm;
  struct mg_rpc *c = mgos_rpc_get_global();
  mg_rpc_add_handler(c, "Shelly.GetInfo", "", GetInfoHandler, nullptr);
  if (server != nullptr) {
    mg_rpc_add_handler(c, "Shelly.GetInfoExt", "", GetInfoExtHandler, nullptr);
    mg_rpc_add_handler(c, "Shelly.SetConfig", "{id: %d, type: %d, config: %T}",
                       SetConfigHandler, nullptr);
    mg_rpc_add_handler(c, "Shelly.SetState", "{id: %d, type: %d, state: %T}",
                       SetStateHandler, nullptr);
    mg_rpc_add_handler(c, "Shelly.Identify", "{id: %d, type: %d}",
                       IdentifyHandler, nullptr);
    mg_rpc_add_handler(c, "Shelly.InjectInputEvent", "{id: %d, event: %d}",
                       InjectInputEventHandler, nullptr);
    mg_rpc_add_handler(c, "Shelly.Abort", "", AbortHandler, nullptr);
    mg_rpc_add_handler(c, "Shelly.SetAuth", "{user: %Q, realm: %Q, ha1: %Q}",
                       SetAuthHandler, nullptr);
    mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.GetWifiConfig", "",
                       GetWifiConfigHandler, nullptr);
    mg_rpc_add_handler(c, "Shelly.SetWifiConfig",
                       ("{ap: {enable: %B, ssid: %Q, pass: %Q}, "
                        "sta: {enable: %B, ssid: %Q, pass: %Q, "
                        "ip: %Q, netmask: %Q, gw: %Q, nameserver: %Q}, "
                        "sta1: {enable: %B, ssid: %Q, pass: %Q, "
                        "ip: %Q, netmask: %Q, gw: %Q, nameserver: %Q}, "
                        "sta_ps_mode: %d}"),
                       SetWifiConfigHandler, nullptr);
  }
  mg_rpc_add_handler(c, "Shelly.GetDebugInfo", "", GetDebugInfoHandler,
                     nullptr);
  mg_rpc_add_handler(c, "Shelly.WipeDevice", "", WipeDeviceHandler, nullptr);
  PublishHTTP();  // Update TXT records for the HTTP service.
  return true;
}

}  // namespace shelly
