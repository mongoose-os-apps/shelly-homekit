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
  void Start();

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
  static Status ValidateAPConfig(const WifiAPConfig &cfg);
  static void STAConfigFromSys(struct mgos_config_wifi_sta &scfg,
                               WifiSTAConfig &cfg);
  static bool STAConfigToSys(const WifiSTAConfig &cfg,
                             struct mgos_config_wifi_sta &scfg);
  static Status ValidateSTAConfig(const WifiSTAConfig &cfg);
  void Process();
  void SaveConfig();
  void CheckAPEnabled();
  void SetState(State state);
  void WifiEvent(int ev, void *ev_data);

  State state_ = State::kIdle;
  double last_change_ = 0;
  bool ap_running_ = false;
  bool connect_failed_ = false;
  WifiConfig cur_;
  WifiConfig new_;
  WifiConfig *act_;
  bool ap_config_changed_ = false;
  double last_ap_client_active_ = 0;

  mgos::Timer process_timer_;
};

static std::unique_ptr<WifiConfigManager> s_mgr;

static constexpr const char *ns(const char *s) {
  return (s != nullptr ? s : "");
}

WifiConfigManager::WifiConfigManager()
    : process_timer_(std::bind(&WifiConfigManager::Process, this)) {
  cur_.ap.enable = mgos_sys_config_get_wifi_ap_keep_enabled();
  cur_.ap.ssid = ns(mgos_sys_config_get_wifi_ap_ssid());
  mgos_expand_mac_address_placeholders((char *) cur_.ap.ssid.data());
  cur_.ap.pass = ns(mgos_sys_config_get_wifi_ap_pass());
  ap_running_ = mgos_sys_config_get_wifi_ap_enable();
  STAConfigFromSys(mgos_sys_config.wifi.sta, cur_.sta);
  STAConfigFromSys(mgos_sys_config.wifi.sta1, cur_.sta1);
  cur_.sta_ps_mode = mgos_sys_config_get_wifi_sta_ps_mode();
  mgos_event_add_group_handler(
      MGOS_WIFI_EV_BASE,
      [](int ev, void *ev_data, void *arg) {
        static_cast<WifiConfigManager *>(arg)->WifiEvent(ev, ev_data);
      },
      this);
  act_ = &cur_;
}

WifiConfigManager::~WifiConfigManager() {
}

void WifiConfigManager::Start() {
  process_timer_.Reset(1000, MGOS_TIMER_REPEAT | MGOS_TIMER_RUN_NOW);
}

Status WifiConfigManager::SetConfig(const WifiConfig &cfg) {
  Status st = ValidateAPConfig(cfg.ap);
  if (!st.ok()) {
    return mgos::Annotatef(st, "Invalid %s config", "AP");
  }
  st = ValidateSTAConfig(cfg.sta);
  if (!st.ok()) {
    return mgos::Annotatef(st, "Invalid %s config", "STA");
  }
  st = ValidateSTAConfig(cfg.sta1);
  if (!st.ok()) {
    return mgos::Annotatef(st, "Invalid %s config", "STA1");
  }
  if (cfg.sta_ps_mode < 0 || cfg.sta_ps_mode > 2) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "Invalid %s", "sta_ps_mode");
  }
  bool sta_config_changed =
      (!(cfg.sta == cur_.sta) || !(cfg.sta1 == cur_.sta1) ||
       (cfg.sta_ps_mode != mgos_sys_config_get_wifi_sta_ps_mode()));
  ap_config_changed_ = !(cfg.ap == cur_.ap);
  LOG(LL_INFO, ("New config: %s %d %d", cfg.ToJSON().c_str(),
                sta_config_changed, ap_config_changed_));
  if (!sta_config_changed && !ap_config_changed_) return Status::OK();
  new_ = cfg;
  connect_failed_ = false;
  if (sta_config_changed) {
    mgos_sys_config_set_wifi_sta_ps_mode(new_.sta_ps_mode);
    LOG(LL_INFO, ("Setting ps mode to %d", new_.sta_ps_mode));
    act_ = &new_;
    SetState(State::kDisconnect);
  }
  if (ap_config_changed_) {
    cur_.ap = new_.ap;
  }
  return Status::OK();
}

WifiConfig WifiConfigManager::GetConfig() {
  return *act_;
}

void WifiConfigManager::ResetConfig() {
  bool should_reset_ap = !(cur_.sta.enable || cur_.sta1.enable);
  LOG(LL_INFO, ("Resetting %s settings", "STA"));
  cur_.sta.enable = false;
  cur_.sta1.enable = false;
  // If called while no STA is configured, reset AP settings as well.
  if (should_reset_ap) {
    LOG(LL_INFO, ("Resetting %s settings", "AP"));
    cur_.ap.ssid = ns(mgos_config_get_default_wifi_ap_ssid());
    mgos_expand_mac_address_placeholders(
        const_cast<char *>(cur_.ap.ssid.c_str()));
    cur_.ap.pass = ns(mgos_config_get_default_wifi_ap_pass());
    ap_config_changed_ = true;
  }
  SaveConfig();
  // AP will be enabled automatically since no STA is configured.
  SetState(State::kDisconnect);
}

WifiInfo WifiConfigManager::GetInfo() {
  WifiInfo info;
  info.ap_running = ap_running_;
  switch (state_) {
    case State::kIdle:
      info.status = "Not connected";
      break;
    case State::kDisconnect:
    case State::kDisconnecting:
      if (new_.sta.enable || new_.sta1.enable) {
        info.status = "Connecting";
      } else {
        info.status = "Disconnecting";
      }
      break;
    case State::kConnect:
    case State::kConnecting:
      info.sta_connecting = true;
      info.status = "Connecting";
      break;
    case State::kConnected:
      info.status = "Connected";
      info.sta_connected = true;
      break;
  }
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
  if (connect_failed_) {
    info.status += " (reverted)";
  }
  if (info.ap_running) {
    info.status += ", AP active";
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
      if (mgos_uptime() - last_change_ < 1) break;
      mgos_wifi_disconnect();
      SetState(State::kDisconnecting);
      break;
    }
    case State::kDisconnecting:
      if (mgos_uptime() - last_change_ < 1) break;
      SetState(State::kConnect);
      break;
    case State::kConnect: {
      bool enabled = false;
      mgos_wifi_sta_clear_cfgs();
      if (act_->sta.enable) {
        mgos_config_wifi_sta cfg;
        mgos_config_wifi_sta_set_defaults(&cfg);
        STAConfigToSys(act_->sta, cfg);
        mgos_wifi_sta_add_cfg(&cfg);
        mgos_config_wifi_sta_free(&cfg);
        enabled = true;
      }
      std::string sp = ScreenPassword(act_->sta.pass);
      LOG(LL_INFO, ("STA  config: %d %s %s", act_->sta.enable,
                    act_->sta.ssid.c_str(), sp.c_str()));
      if (act_->sta1.enable) {
        mgos_config_wifi_sta cfg;
        mgos_config_wifi_sta_set_defaults(&cfg);
        STAConfigToSys(act_->sta1, cfg);
        mgos_wifi_sta_add_cfg(&cfg);
        mgos_config_wifi_sta_free(&cfg);
        enabled = true;
      }
      std::string sp1 = ScreenPassword(act_->sta1.pass);
      LOG(LL_INFO, ("STA1 config: %d %s %s", act_->sta1.enable,
                    act_->sta1.ssid.c_str(), sp1.c_str()));
      if (enabled) {
        mgos_wifi_connect();
        SetState(State::kConnecting);
      } else {
        mgos_config_wifi_sta cfg;
        mgos_config_wifi_sta_set_defaults(&cfg);
        cfg.enable = false;
        mgos_wifi_setup_sta(&cfg);
        mgos_config_wifi_sta_free(&cfg);
        if (act_ == &new_) {
          cur_ = new_;
          act_ = &cur_;
        }
        SetState(State::kIdle);
      }
      break;
    }
    case State::kConnecting: {
      if (mgos_wifi_get_status() == MGOS_WIFI_IP_ACQUIRED) {
        if (act_ == &new_) {
          // Ok, this config worked, make current and save.
          cur_ = new_;
          act_ = &cur_;
          SaveConfig();
        }
        SetState(State::kConnected);
        break;
      }
      if (mgos_uptime() - last_change_ >
          mgos_sys_config_get_wifi_sta_connect_timeout() * 2) {
        LOG(LL_ERROR, ("Connection failed"));
        connect_failed_ = true;
        if (act_ == &new_) {
          LOG(LL_INFO,
              ("Reverting to previous config: %s", cur_.ToJSON().c_str()));
          act_ = &cur_;
        }
        SetState(State::kDisconnect);
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
  if (scfg.keep_enabled != cfg.enable) {
    scfg.keep_enabled = cfg.enable;
    changed = true;
  }
  changed |= SetStrIfChanged(cfg.ssid, &scfg.ssid);
  changed |= SetStrIfChanged(cfg.pass, &scfg.pass);
  return changed;
}

// static
Status WifiConfigManager::ValidateAPConfig(const WifiAPConfig &cfg) {
  struct mgos_config_wifi_ap scfg;
  mgos_config_wifi_ap_set_defaults(&scfg);
  Status st;
  char *err_msg = NULL;
  APConfigToSys(cfg, scfg);
  if (!mgos_wifi_validate_ap_cfg(&scfg, &err_msg)) {
    st = mgos::Errorf(STATUS_INVALID_ARGUMENT, "%s", err_msg);
  }
  mgos_config_wifi_ap_free(&scfg);
  free(err_msg);
  return st;
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
  cfg.nameserver = ns(scfg.nameserver);
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
  changed |= SetStrIfChanged(cfg.nameserver, &scfg.nameserver);
  return changed;
}

// static
Status WifiConfigManager::ValidateSTAConfig(const WifiSTAConfig &cfg) {
  struct mgos_config_wifi_sta scfg;
  mgos_config_wifi_sta_set_defaults(&scfg);
  Status st;
  char *err_msg = NULL;
  STAConfigToSys(cfg, scfg);
  if (!mgos_wifi_validate_sta_cfg(&scfg, &err_msg)) {
    st = mgos::Errorf(STATUS_INVALID_ARGUMENT, "%s", err_msg);
  }
  mgos_config_wifi_sta_free(&scfg);
  free(err_msg);
  return st;
}

void WifiConfigManager::SaveConfig() {
  bool changed = false;
  struct mgos_config_wifi wcfg;
  mgos_config_wifi_set_defaults(&wcfg);
  changed |= APConfigToSys(cur_.ap, wcfg.ap);
  changed |= STAConfigToSys(cur_.sta, wcfg.sta);
  changed |= STAConfigToSys(cur_.sta1, wcfg.sta1);
  if (wcfg.sta_ps_mode != cur_.sta_ps_mode) {
    wcfg.sta_ps_mode = cur_.sta_ps_mode;
    changed = true;
  }
  if (changed) {
    mgos_config_wifi_copy(&wcfg, &mgos_sys_config.wifi);
    mgos_sys_config_save(&mgos_sys_config, false /* try_once */,
                         nullptr /* msg */);
  }
  mgos_config_wifi_free(&wcfg);
}

void WifiConfigManager::CheckAPEnabled() {
  // We want AP enabled if there is no STA config
  bool want_ap = cur_.ap.enable;
  if (!(cur_.sta.enable || cur_.sta1.enable)) want_ap = true;
  if (want_ap == ap_running_ && !ap_config_changed_) return;
  // Delay any changes until boot time connection has had a chance to settle.
  if ((cur_.sta.enable || cur_.sta1.enable) && mgos_uptime() < 10) {
    return;
  }
  // Do not disable AP if there are active clients.
  if (!want_ap && last_ap_client_active_ > 0 &&
      (mgos_uptime() - last_ap_client_active_) < 60) {
    return;
  }
  struct mgos_config_wifi_ap cfg;
  mgos_config_wifi_ap_set_defaults(&cfg);
  cfg.enable = want_ap;
  mgos_conf_set_str(&cfg.ssid, cur_.ap.ssid.c_str());
  mgos_conf_set_str(&cfg.pass, cur_.ap.pass.c_str());
  LOG(LL_INFO, ("%s AP %s", (want_ap ? "Enabling" : "Disabling"), cfg.ssid));
  if (mgos_wifi_setup_ap(&cfg)) {
    ap_config_changed_ = false;
    ap_running_ = want_ap;
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
    if (state_ != State::kDisconnect) {
      SetState(State::kConnecting);
    }
  }
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

void InitWifiConfigManager() {
  s_mgr.reset(new WifiConfigManager());
}

void StartWifiConfigManager() {
  s_mgr->Start();
}

#if CS_PLATFORM == CS_P_ESP8266
extern "C" {
#include <user_interface.h>
}
std::string GetMACAddr(bool sta, bool delims) {
  uint8_t mac[6];
  wifi_get_macaddr((sta ? STATION_IF : SOFTAP_IF), mac);
  return FormatMACAddr(mac, delims);
}
#elif CS_PLATFORM == CS_P_ESP32
#include "esp_mac.h"
std::string GetMACAddr(bool sta, bool delims) {
  uint8_t mac[6];
  esp_read_mac(mac, (sta ? ESP_MAC_WIFI_STA : ESP_MAC_WIFI_SOFTAP));
  return FormatMACAddr(mac, delims);
}
#endif

}  // namespace shelly
