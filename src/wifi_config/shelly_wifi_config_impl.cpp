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

#include "shelly_wifi_config.hpp"

#include "mgos.hpp"
#include "mgos_net.h"
#include "mgos_rpc.h"
#include "mgos_wifi.h"
#include "mgos_wifi_sta.h"

#include "shelly_rpc_service.hpp"

namespace shelly {

class WifiConfigManager {
 public:
  WifiConfigManager();
  WifiConfigManager(const WifiConfigManager &other) = delete;
  ~WifiConfigManager();

  Status SetConfig(const WifiConfig &cfg);
  WifiConfig GetConfig();
  void ResetConfig();
  WifiInfo GetInfo();

  void ReportClientRequest(const std::string &client_addr);

 private:
  enum class State {
    kIdle = 0,
    kDisconnect = 1,
    kDisconnecting = 2,
    kConnect = 3,
    kConnecting = 4,
    kConnected = 5,
  };

  static bool APConfigToSys(const WifiAPConfig &cfg,
                            struct mgos_config_wifi_ap &scfg);
  static void STAConfigFromSys(struct mgos_config_wifi_sta &scfg,
                               WifiSTAConfig &cfg);
  static bool STAConfigToSys(const WifiSTAConfig &cfg,
                             struct mgos_config_wifi_sta &scfg);
  void Process();
  void SaveConfig();
  void CheckAPEnabled();
  void SetState(State state);
  void WifiEvent(int ev, void *ev_data);

  State state_ = State::kIdle;
  double last_change_ = 0;
  bool ap_enabled_ = false;
  bool connect_failed_ = false;
  WifiConfig current_;
  bool have_backup_ = false;
  WifiConfig backup_;
  double last_ap_client_active_ = 0;
  bool ap_config_changed_ = false;

  mgos::Timer process_timer_;
};

static std::unique_ptr<WifiConfigManager> s_mgr;

static constexpr const char *ns(const char *s) {
  return (s != nullptr ? s : "");
}

WifiConfigManager::WifiConfigManager()
    : process_timer_(std::bind(&WifiConfigManager::Process, this)) {
  void APConfigFromSys(struct mgos_config_wifi_ap & scfg, WifiAPConfig & cfg);
  current_.ap.enable = mgos_sys_config_get_wifi_ap_enable();
  current_.ap.ssid = ns(mgos_sys_config_get_wifi_ap_ssid());
  current_.ap.pass = ns(mgos_sys_config_get_wifi_ap_pass());
  ap_enabled_ = mgos_sys_config_get_wifi_ap_enable();
  STAConfigFromSys(mgos_sys_config.wifi.sta, current_.sta);
  STAConfigFromSys(mgos_sys_config.wifi.sta1, current_.sta1);
  mgos_event_add_group_handler(
      MGOS_WIFI_EV_BASE,
      [](int ev, void *ev_data, void *arg) {
        static_cast<WifiConfigManager *>(arg)->WifiEvent(ev, ev_data);
      },
      this);
  process_timer_.Reset(1000, MGOS_TIMER_REPEAT);
}

WifiConfigManager::~WifiConfigManager() {
}

Status WifiConfigManager::SetConfig(const WifiConfig &cfg) {
  bool sta_changed = !(cfg.sta == current_.sta) || !(cfg.sta1 == current_.sta1);
  ap_config_changed_ = !(cfg.ap == current_.ap);
  current_ = cfg;
  if (sta_changed) SetState(State::kDisconnect);
  return Status::OK();
}

WifiConfig WifiConfigManager::GetConfig() {
  return current_;
}

void WifiConfigManager::ResetConfig() {
  bool should_reset_ap = !(current_.sta.enable || current_.sta1.enable);
  LOG(LL_INFO, ("Resetting %s settings", "STA"));
  current_.sta.enable = false;
  current_.sta1.enable = false;
  // If called while no STA is configured, reset AP settings as well.
  if (should_reset_ap) {
    LOG(LL_INFO, ("Resetting %s settings", "AP"));
    current_.ap.ssid = ns(mgos_config_get_default_wifi_ap_ssid());
    mgos_expand_mac_address_placeholders(
        const_cast<char *>(current_.ap.ssid.c_str()));
    current_.ap.pass = ns(mgos_config_get_default_wifi_ap_pass());
    ap_config_changed_ = true;
  }
  SaveConfig();
  // AP will be enabled automatically since no STA is configured.
  SetState(State::kDisconnect);
}

WifiInfo WifiConfigManager::GetInfo() {
  WifiInfo info;
  info.ap_enabled = ap_enabled_;
  info.sta_connecting = (state_ == State::kConnecting);
  info.sta_connected = (state_ == State::kConnected);
  if (info.sta_connected) {
    info.sta_rssi = mgos_wifi_sta_get_rssi();
    std::unique_ptr<char> ssid(mgos_wifi_get_connected_ssid());
    if (ssid != nullptr) info.sta_ssid = ssid.get();
    char ip_str[16] = {};
    struct mgos_net_ip_info ip_info = {};
    if (mgos_net_get_ip_info(MGOS_NET_IF_TYPE_WIFI, MGOS_NET_IF_WIFI_STA,
                             &ip_info)) {
      mgos_net_ip_to_str(&ip_info.ip, ip_str);
      info.sta_ip = ip_str;
    }
  }
  return info;
}

void WifiConfigManager::ReportClientRequest(const std::string &client_addr) {
  bool is_ap = false;
  // TODO(rojer): Proper ip/netmask comparison.
  std::string ap_ip_prefix(mgos_sys_config_get_wifi_ap_ip(),
                           strlen(mgos_sys_config_get_wifi_ap_ip()) - 1);
  if (client_addr.substr(0, ap_ip_prefix.length()) == ap_ip_prefix) {
    last_ap_client_active_ = mgos_uptime();
    is_ap = true;
  }
  LOG(LL_DEBUG,
      ("Client activity from %s is_ap %s", client_addr.c_str(), YesNo(is_ap)));
}

void WifiConfigManager::Process() {
  switch (state_) {
    case State::kIdle:
      if (mgos_wifi_get_status() == MGOS_WIFI_IP_ACQUIRED) {
        SetState(State::kConnected);
        break;
      }
      CheckAPEnabled();
      break;
    case State::kDisconnect: {
      connect_failed_ = false;
      mgos_wifi_disconnect();
      SetState(State::kDisconnecting);
      break;
    }
    case State::kDisconnecting:
      if (mgos_uptime() - last_change_ < 1) break;
      SetState(State::kConnect);
      break;
    case State::kConnect: {
      connect_failed_ = false;
      bool enabled = false;
      mgos_wifi_sta_clear_cfgs();
      if (current_.sta.enable) {
        mgos_config_wifi_sta cfg;
        mgos_config_wifi_sta_set_defaults(&cfg);
        STAConfigToSys(current_.sta, cfg);
        mgos_wifi_sta_add_cfg(&cfg);
        mgos_config_wifi_sta_free(&cfg);
        enabled = true;
      }
      std::string sp = ScreenPassword(current_.sta.pass);
      LOG(LL_INFO, ("STA  config: %d %s %s", current_.sta.enable,
                    current_.sta.ssid.c_str(), sp.c_str()));
      if (current_.sta1.enable) {
        mgos_config_wifi_sta cfg;
        mgos_config_wifi_sta_set_defaults(&cfg);
        STAConfigToSys(current_.sta1, cfg);
        mgos_wifi_sta_add_cfg(&cfg);
        mgos_config_wifi_sta_free(&cfg);
        enabled = true;
      }
      std::string sp1 = ScreenPassword(current_.sta1.pass);
      LOG(LL_INFO, ("STA1 config: %d %s %s", current_.sta1.enable,
                    current_.sta1.ssid.c_str(), sp1.c_str()));
      if (enabled) {
        mgos_wifi_connect();
        SetState(State::kConnecting);
      } else {
        mgos_config_wifi_sta cfg;
        mgos_config_wifi_sta_set_defaults(&cfg);
        cfg.enable = false;
        mgos_wifi_setup_sta(&cfg);
        mgos_config_wifi_sta_free(&cfg);
        SetState(State::kIdle);
      }
      break;
    }
    case State::kConnecting: {
      if (mgos_wifi_get_status() == MGOS_WIFI_IP_ACQUIRED) {
        // Ok, this config worked, save it as backup.
        backup_ = current_;
        have_backup_ = true;
        SaveConfig();
        SetState(State::kConnected);
        break;
      }
      if (mgos_uptime() - last_change_ >
          mgos_sys_config_get_wifi_sta_connect_timeout() * 2) {
        LOG(LL_ERROR, ("Connection failed"));
        connect_failed_ = true;
      }
      break;
    }
    case State::kConnected: {
      if (mgos_wifi_get_status() != MGOS_WIFI_IP_ACQUIRED) {
        SetState(State::kIdle);
        break;
      }
      CheckAPEnabled();
      break;
    }
  }
}

static bool SetStrIfChanged(const std::string &s, const char **ss) {
  if (s == ns(*ss)) return false;
  mgos_conf_set_str(ss, s.c_str());
  return true;
}

// static
bool WifiConfigManager::APConfigToSys(const WifiAPConfig &cfg,
                                      struct mgos_config_wifi_ap &scfg) {
  bool changed = false;
  if (scfg.enable != cfg.enable) {
    scfg.enable = cfg.enable;
    changed = true;
  }
  changed |= SetStrIfChanged(cfg.ssid, &scfg.ssid);
  changed |= SetStrIfChanged(cfg.pass, &scfg.pass);
  return changed;
}

// static
void WifiConfigManager::STAConfigFromSys(struct mgos_config_wifi_sta &scfg,
                                         WifiSTAConfig &cfg) {
  cfg.enable = scfg.enable;
  cfg.ssid = ns(scfg.ssid);
  cfg.pass = ns(scfg.pass);
  cfg.ip = ns(scfg.ip);
  cfg.netmask = ns(scfg.netmask);
  cfg.gw = ns(scfg.gw);
}

// static
bool WifiConfigManager::STAConfigToSys(const WifiSTAConfig &cfg,
                                       struct mgos_config_wifi_sta &scfg) {
  bool changed = false;
  if (scfg.enable != cfg.enable) {
    scfg.enable = cfg.enable;
    changed = true;
  }
  changed |= SetStrIfChanged(cfg.ssid, &scfg.ssid);
  changed |= SetStrIfChanged(cfg.pass, &scfg.pass);
  changed |= SetStrIfChanged(cfg.ip, &scfg.ip);
  changed |= SetStrIfChanged(cfg.netmask, &scfg.netmask);
  changed |= SetStrIfChanged(cfg.gw, &scfg.gw);
  return changed;
}

void WifiConfigManager::SaveConfig() {
  bool changed = false;
  struct mgos_config_wifi wcfg;
  mgos_config_wifi_set_defaults(&wcfg);
  changed |= APConfigToSys(current_.ap, wcfg.ap);
  changed |= STAConfigToSys(current_.sta, wcfg.sta);
  changed |= STAConfigToSys(current_.sta1, wcfg.sta1);
  if (changed) {
    mgos_config_wifi_copy(&wcfg, &mgos_sys_config.wifi);
    mgos_sys_config_save(&mgos_sys_config, false /* try_once */,
                         nullptr /* msg */);
  }
  mgos_config_wifi_free(&wcfg);
}

void WifiConfigManager::CheckAPEnabled() {
  // We want AP enabled if there is no STA config
  bool want_ap = current_.ap.enable;
  if (!(current_.sta.enable || current_.sta1.enable)) want_ap = true;
  if (connect_failed_) want_ap = true;
  if (want_ap == ap_enabled_ && !ap_config_changed_) return;
  // Delay any changes until boot time connection has had a chance to settle.
  if ((current_.sta.enable || current_.sta1.enable) && mgos_uptime() < 10) {
    return;
  }
  // Do not disable AP if there are active clients.
  if (!want_ap && (mgos_uptime() - last_ap_client_active_) < 60) {
    return;
  }
  struct mgos_config_wifi_ap cfg;
  mgos_config_wifi_ap_set_defaults(&cfg);
  cfg.enable = want_ap;
  mgos_conf_set_str(&cfg.ssid, current_.ap.ssid.c_str());
  mgos_conf_set_str(&cfg.pass, current_.ap.pass.c_str());
  LOG(LL_INFO, ("%s AP %s", (want_ap ? "Enabling" : "Disabling"), cfg.ssid));
  if (mgos_wifi_setup_ap(&cfg)) {
    ap_config_changed_ = false;
    ap_enabled_ = want_ap;
    SaveConfig();
  }
  mgos_config_wifi_ap_free(&cfg);
}

void WifiConfigManager::SetState(State state) {
  if (state == state_) return;
  LOG(LL_DEBUG, ("WifiMgr state %d -> %d", (int) state_, (int) state));
  state_ = state;
  last_change_ = mgos_uptime();
  mgos::InvokeCB(std::bind(&WifiConfigManager::Process, this));
}

void WifiConfigManager::WifiEvent(int ev, void *) {
  // This catches initial automatic connection on boot.
  if (ev == MGOS_WIFI_EV_STA_CONNECTING) {
    SetState(State::kConnecting);
  }
  Process();
}

WifiConfig GetWifiConfig() {
  return s_mgr->GetConfig();
}

Status SetWifiConfig(const WifiConfig &cfg) {
  return s_mgr->SetConfig(cfg);
}

void ResetWifiConfig() {
  s_mgr->ResetConfig();
}

WifiInfo GetWifiInfo() {
  return s_mgr->GetInfo();
}

void ReportClientRequest(const std::string &client_addr) {
  s_mgr->ReportClientRequest(client_addr);
}

static void GetWifiConfigHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                                 struct mg_rpc_frame_info *fi,
                                 struct mg_str args) {
  std::string cfg_json = s_mgr->GetConfig().ToJSON();
  mg_rpc_send_responsef(ri, "%s", cfg_json.c_str());
  (void) cb_arg;
  (void) fi;
  (void) args;
}

static void SetWifiConfigHandler(struct mg_rpc_request_info *ri, void *cb_arg,
                                 struct mg_rpc_frame_info *fi,
                                 struct mg_str args) {
  ReportRPCRequest(ri);
  WifiConfig cfg = s_mgr->GetConfig();
  int8_t ap_enable = -1, sta_enable = -1, sta1_enable = -1;
  char *ap_ssid = nullptr, *ap_pass = nullptr;
  char *sta_ssid = nullptr, *sta_pass = nullptr;
  char *sta1_ssid = nullptr, *sta1_pass = nullptr;
  json_scanf(args.p, args.len, ri->args_fmt, &ap_enable, &ap_ssid, &ap_pass,
             &sta_enable, &sta_ssid, &sta_pass, &sta1_enable, &sta1_ssid,
             &sta1_pass);
  mgos::ScopedCPtr o1(ap_ssid), o2(ap_pass);
  mgos::ScopedCPtr o3(sta_ssid), o4(sta_pass), o5(sta1_ssid), o6(sta1_pass);
  if (ap_enable != -1) cfg.ap.enable = ap_enable;
  if (ap_ssid != nullptr) cfg.ap.ssid = ap_ssid;
  if (ap_pass != nullptr) cfg.ap.pass = ap_pass;
  if (sta_enable != -1) cfg.sta.enable = sta_enable;
  if (sta_ssid != nullptr) cfg.sta.ssid = sta_ssid;
  if (sta_pass != nullptr) cfg.sta.pass = sta_pass;
  if (sta1_enable != -1) cfg.sta1.enable = sta1_enable;
  if (sta1_ssid != nullptr) cfg.sta1.ssid = sta1_ssid;
  if (sta1_pass != nullptr) cfg.sta1.pass = sta1_pass;
  Status st = s_mgr->SetConfig(cfg);
  SendStatusResp(ri, st);
  (void) cb_arg;
  (void) fi;
  (void) args;
}

void InitWifiConfigManager() {
  s_mgr.reset(new WifiConfigManager());
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.GetWifiConfig", "",
                     GetWifiConfigHandler, nullptr);
  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetWifiConfig",
                     ("{ap: {enable: %B, ssid: %Q, pass: %Q}, "
                      "sta: {enable: %B, ssid: %Q, pass: %Q}, "
                      "sta1: {enable: %B, ssid: %Q, pass: %Q}}"),
                     SetWifiConfigHandler, nullptr);
}

}  // namespace shelly
