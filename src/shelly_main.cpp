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

#include "shelly_main.hpp"

#include <math.h>

#include "mgos.hpp"
#include "mgos_app.h"
#include "mgos_hap.h"
#include "mgos_http_server.h"
#include "mgos_ota.h"
#include "mgos_rpc.h"

#include "mongoose.h"

#if CS_PLATFORM == CS_P_ESP8266
#include "esp_coredump.h"
#include "esp_rboot.h"
#endif

#include "HAP.h"
#include "HAPAccessoryServer+Internal.h"
#include "HAPPlatform+Init.h"
#include "HAPPlatformAccessorySetup+Init.h"
#include "HAPPlatformBLEPeripheralManager+Init.h"
#include "HAPPlatformKeyValueStore+Init.h"
#include "HAPPlatformMFiTokenAuth+Init.h"
#include "HAPPlatformServiceDiscovery+Init.h"
#include "HAPPlatformTCPStreamManager+Init.h"

#include "shelly_debug.hpp"
#include "shelly_hap_garage_door_opener.hpp"
#include "shelly_hap_humidity_sensor.hpp"
#include "shelly_hap_input.hpp"
#include "shelly_hap_lock.hpp"
#include "shelly_hap_outlet.hpp"
#include "shelly_hap_switch.hpp"
#include "shelly_hap_temperature_sensor.hpp"
#include "shelly_hap_valve.hpp"
#include "shelly_input.hpp"
#include "shelly_ota.hpp"
#include "shelly_output.hpp"
#include "shelly_rpc_service.hpp"
#include "shelly_switch.hpp"
#include "shelly_sys_led_btn.hpp"
#include "shelly_temp_sensor.hpp"
#include "shelly_wifi_config.hpp"

#define SCRATCH_BUF_SIZE 1536

namespace shelly {

static HAPIPSession sessions[MAX_NUM_HAP_SESSIONS];
static uint8_t scratch_buf[SCRATCH_BUF_SIZE];
static HAPIPAccessoryServerStorage s_ip_storage = {
    .sessions = sessions,
    .numSessions = ARRAY_SIZE(sessions),
    .scratchBuffer =
        {
            .bytes = scratch_buf,
            .numBytes = sizeof(scratch_buf),
        },
};

#ifdef MGOS_HAVE_BT_COMMON
// TODO: make dynamic
static HAPBLEGATTTableElementRef gattTableElements[100];
static HAPBLESessionCacheElementRef
    sessionCacheElements[kHAPBLESessionCache_MinElements];
static HAPSessionRef session;
static uint8_t procedureBytes[2048];
static HAPBLEProcedureRef procedures[1];

static HAPPlatformBLEPeripheralManager s_blepm;
static HAPBLEAccessoryServerStorage s_ble_storage = {
    .gattTableElements = gattTableElements,
    .numGATTTableElements = HAPArrayCount(gattTableElements),
    .sessionCacheElements = sessionCacheElements,
    .numSessionCacheElements = HAPArrayCount(sessionCacheElements),
    .session = &session,
    .procedures = procedures,
    .numProcedures = HAPArrayCount(procedures),
    .procedureBuffer =
        {
            .bytes = procedureBytes,
            .numBytes = sizeof procedureBytes,
        },
};
#endif

static HAPPlatformKeyValueStore s_kvs;
static HAPPlatformAccessorySetup s_accessory_setup;
static HAPAccessoryServerCallbacks s_callbacks;
static HAPPlatformTCPStreamManager s_tcpm;
static HAPPlatformServiceDiscovery s_service_discovery;
static HAPAccessoryServerRef s_server;
static mgos::hap::Accessory::IdentifyCB s_identify_cb;

static uint8_t s_service_flags = 0;

static std::vector<std::unique_ptr<Input>> s_inputs;
static std::vector<std::unique_ptr<Output>> s_outputs;
static std::vector<std::unique_ptr<PowerMeter>> s_pms;
static std::vector<std::unique_ptr<mgos::hap::Accessory>> s_accs;
static std::vector<const HAPAccessory *> s_hap_accs;
static std::unique_ptr<TempSensor> s_sys_temp_sensor;
std::vector<std::unique_ptr<Component>> g_comps;

void RestoreUART() {
  struct mgos_uart_config ucfg;
  int uart_no = 0;
  if (mgos_uart_config_get(uart_no, &ucfg)) {
    if (!mgos_uart_configure(uart_no, &ucfg)) {
      LOG(LL_ERROR, ("Failed to configure UART%d", uart_no));
    }
  }
}

bool DetectAddon(int pin_in, int pin_out) {
  if (pin_in == -1 || pin_out == -1) {
    return false;
  }
  // case 1: input with pull up
  mgos_gpio_setup_input(pin_in, MGOS_GPIO_PULL_UP);
  // check if pulled by something external, not check output to input yet
  bool active = mgos_gpio_read(pin_in);
  if (!active) {
    // something is pulling us low, we might have an addon with switchss
    return true;
  }

  // Try to pull low via addon
  mgos_gpio_setup_output(pin_out, 0 /* LOW */);
  mgos_gpio_setup_input(pin_in, MGOS_GPIO_PULL_NONE);
  return !mgos_gpio_read(pin_in);
}
template <class T>
T *FindById(const std::vector<std::unique_ptr<T>> &vv, int id) {
  for (auto &v : vv) {
    if (v->id() == id) return v.get();
  }
  return nullptr;
}
Input *FindInput(int id) {
  return FindById(s_inputs, id);
}
Output *FindOutput(int id) {
  return FindById(s_outputs, id);
}
PowerMeter *FindPM(int id) {
  return FindById(s_pms, id);
}

void CreateHAPSensors(std::vector<std::unique_ptr<TempSensor>> *sensors,
                      std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  struct mgos_config_ts *ts_cfgs[] = {
#ifdef MGOS_CONFIG_HAVE_TS1
      (struct mgos_config_ts *) mgos_sys_config_get_ts1(),
#endif
#ifdef MGOS_CONFIG_HAVE_TS2
      (struct mgos_config_ts *) mgos_sys_config_get_ts2(),
#endif
#ifdef MGOS_CONFIG_HAVE_TS3
      (struct mgos_config_ts *) mgos_sys_config_get_ts3(),
#endif
#ifdef MGOS_CONFIG_HAVE_TS4
      (struct mgos_config_ts *) mgos_sys_config_get_ts4(),
#endif
#ifdef MGOS_CONFIG_HAVE_TS5
      (struct mgos_config_ts *) mgos_sys_config_get_ts5(),
#endif
  };
  size_t j = 0;
  for (size_t i = 0; i < std::min((size_t) sizeof(ts_cfgs), sensors->size());
       i++) {
    auto *ts_cfg = ts_cfgs[i];
    TempSensor *ts = ((*sensors)[i].get());
    shelly::hap::CreateHAPTemperatureSensor(j++, ts, ts_cfg, comps, accs, svr);

    if (ts->getType() == TS_HUM) {  // can only be one shares config, as same
                                    // update interval but no unit settable
      i++;
      ts_cfg = ts_cfgs[i];
      shelly::hap::CreateHAPHumiditySensor(j++, (HumidityTempSensor *) ts,
                                           ts_cfg, comps, accs, svr);
      break;  // max 1 DHT sensor
    }
  }
}

void CreateHAPSwitch(int id, const struct mgos_config_sw *sw_cfg,
                     const struct mgos_config_in *in_cfg,
                     std::vector<std::unique_ptr<Component>> *comps,
                     std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                     HAPAccessoryServerRef *svr, bool to_pri_acc,
                     Output *led_out) {
  std::unique_ptr<ShellySwitch> sw;
  Input *in = FindInput(id);
  Output *out = FindOutput(id);
  PowerMeter *pm = FindPM(id);
  struct mgos_config_sw *cfg = (struct mgos_config_sw *) sw_cfg;
  uint64_t aid = 0;
  HAPAccessoryCategory cat = kHAPAccessoryCategory_BridgedAccessory;
  bool sw_hidden = false;
  switch (sw_cfg->svc_type) {
    case 0:
      cat = kHAPAccessoryCategory_Switches;
      aid = SHELLY_HAP_AID_BASE_SWITCH + id;
      sw.reset(new hap::Switch(id, in, out, pm, led_out, cfg));
      break;
    case 1:
      cat = kHAPAccessoryCategory_Outlets;
      aid = SHELLY_HAP_AID_BASE_OUTLET + id;
      sw.reset(new hap::Outlet(id, in, out, pm, led_out, cfg));
      break;
    case 2:
      cat = kHAPAccessoryCategory_Locks;
      aid = SHELLY_HAP_AID_BASE_LOCK + id;
      sw.reset(new hap::Lock(id, in, out, pm, led_out, cfg));
      break;
    case 3:
      cat = kHAPAccessoryCategory_Faucets;
      aid = SHELLY_HAP_AID_BASE_VALVE + id;
      sw.reset(new hap::Valve(id, in, out, pm, led_out, cfg));
      break;
    default:
      sw.reset(new ShellySwitch(id, in, out, pm, led_out, cfg));
      sw_hidden = true;
      break;
  }
  auto st = sw->Init();
  if (!st.ok()) {
    const std::string &s = st.ToString();
    LOG(LL_ERROR, ("Error creating switch: %s", s.c_str()));
    return;
  }
  ShellySwitch *sw2 = sw.get();
  comps->push_back(std::move(sw));
  mgos::hap::Accessory *pri_acc = accs->front().get();
  if (to_pri_acc) {
    // NB: this produces duplicate primary services on multi-switch devices in
    // legacy mode. This is necessary to ensure accessory configuration remains
    // exactly the same.
    sw2->set_primary(true);
    pri_acc->SetCategory(cat);
    pri_acc->AddService(sw2);
    // This was requested in
    // https://github.com/mongoose-os-apps/shelly-homekit/issues/237 however,
    // without https://github.com/mongoose-os-libs/dns-sd/issues/5 it causes
    // confusion when multiple accessories advertise the same name (reported in
    // https://github.com/mongoose-os-apps/shelly-homekit/issues/561).
    // So for now we're going back to less readable but unique accessory names.
    // pri_acc->SetName(sw2->name());
    return;
  }
  if (!sw_hidden) {
    std::unique_ptr<mgos::hap::Accessory> acc(
        new mgos::hap::Accessory(aid, kHAPAccessoryCategory_BridgedAccessory,
                                 sw_cfg->name, GetIdentifyCB(), svr));
    acc->AddHAPService(&mgos_hap_accessory_information_service);
    acc->AddService(sw2);
    accs->push_back(std::move(acc));
  }
  if (sw_cfg->in_mode == (int) InMode::kDetached) {
    hap::CreateHAPInput(id, in_cfg, comps, accs, svr);
  }
}

static void DisableLegacyHAPLayout() {
  if (!mgos_sys_config_get_shelly_legacy_hap_layout()) return;
  LOG(LL_INFO, ("Turning off legacy HAP layout"));
  mgos_sys_config_set_shelly_legacy_hap_layout(false);
  mgos_sys_config_save(&mgos_sys_config, false /* try_once */, nullptr);
}

static bool StartService(bool quiet) {
  if (s_service_flags != 0) {
    return false;
  }
  if (HAPAccessoryServerGetState(&s_server) != kHAPAccessoryServerState_Idle) {
    return true;
  }
  if (s_accs.empty()) {
    LOG(LL_INFO, ("=== Creating accessories"));
    std::unique_ptr<mgos::hap::Accessory> pri_acc(new mgos::hap::Accessory(
        SHELLY_HAP_AID_PRIMARY, kHAPAccessoryCategory_Bridges,
        mgos_sys_config_get_shelly_name(), GetIdentifyCB(), &s_server));
    pri_acc->AddHAPService(&mgos_hap_accessory_information_service);
    pri_acc->AddHAPService(&mgos_hap_protocol_information_service);
    pri_acc->AddHAPService(&mgos_hap_pairing_service);
    s_accs.push_back(std::move(pri_acc));
    for (auto &in : s_inputs) {
      in->SetInvert(false);
    }
    for (auto &out : s_outputs) {
      out->SetInvert(false);
    }
    CreateComponents(&g_comps, &s_accs, &s_server);
    s_accs.shrink_to_fit();
    g_comps.shrink_to_fit();
  }

  if (!HAPAccessoryServerIsPaired(&s_server) && !mgos_hap_config_valid()) {
    if (!quiet) {
      LOG(LL_INFO, ("=== Accessory not provisioned"));
    }
    return false;
  }
  HAPDeviceIDString device_id;
  if (HAPDeviceIDGetAsString(&s_kvs, &device_id) != kHAPError_None) {
    device_id.stringValue[0] = '\0';
  }
  const char *uuid = mgos_sys_config_get_hap_mfi_uuid();
  if (uuid == NULL) uuid = "<n/a>";
  uint16_t cn;
  if (HAPAccessoryServerGetCN(&s_kvs, &cn) != kHAPError_None) {
    cn = 0;
  }
  if (s_accs.size() == 1) {
    LOG(LL_INFO, ("=== Starting HAP %s (ID %s, UUID %s, CN %d)", "server",
                  device_id.stringValue, uuid, cn));
    HAPAccessoryServerStart(&s_server, s_accs.front()->GetHAPAccessory());
  } else {
    if (s_hap_accs.empty()) {
      for (auto it = s_accs.begin() + 1; it != s_accs.end(); it++) {
        s_hap_accs.push_back((*it)->GetHAPAccessory());
      }
      s_hap_accs.push_back(nullptr);
      s_hap_accs.shrink_to_fit();
    }
    LOG(LL_INFO,
        ("=== Starting HAP %s (ID %s, UUID %s, CN %d, %d accessories)",
         "bridge", device_id.stringValue, uuid, cn, (int) s_hap_accs.size()));
    HAPAccessoryServerStartBridge(&s_server, s_accs.front()->GetHAPAccessory(),
                                  s_hap_accs.data(),
                                  false /* config changed */);
  }
  return true;
}

static void DestroyComponents() {
  if (s_accs.empty()) return;
  LOG(LL_INFO, ("=== Destroying accessories"));
  s_accs.clear();
  s_hap_accs.clear();
  g_comps.clear();
}

void StopService() {
  HAPAccessoryServerState state = HAPAccessoryServerGetState(&s_server);
  if (state == kHAPAccessoryServerState_Idle) {
    DestroyComponents();
    return;
  }
  LOG(LL_INFO, ("== Stopping HAP service (%d)", state));
  HAPAccessoryServerStop(&s_server);
}

bool IsServiceRunning() {
  return (HAPAccessoryServerGetState(&s_server) ==
          kHAPAccessoryServerState_Running);
}

bool IsPaired() {
  return HAPAccessoryServerIsPaired(&s_server);
}

static void HAPServerStateUpdateCB(HAPAccessoryServerRef *server, void *) {
  HAPAccessoryServerState st = HAPAccessoryServerGetState(server);
  LOG(LL_INFO, ("HAP server state: %d", st));
  if (st == kHAPAccessoryServerState_Idle) {
    // Safe to destroy components now.
    DestroyComponents();
  }
}

static void CheckOverheat(int sys_temp) {
  if (!(s_service_flags & SHELLY_SERVICE_FLAG_OVERHEAT)) {
    if (sys_temp >= mgos_sys_config_get_shelly_overheat_on()) {
      LOG(LL_ERROR, ("== System temperature too high, stopping service"));
      s_service_flags |= SHELLY_SERVICE_FLAG_OVERHEAT;
      StopService();
      for (auto &o : s_outputs) {
        o->SetState(false, "OVH");
      }
    }
  } else {
    if (sys_temp <= mgos_sys_config_get_shelly_overheat_off()) {
      LOG(LL_INFO, ("== System temperature normal, resuming service"));
      s_service_flags &= ~SHELLY_SERVICE_FLAG_OVERHEAT;
    }
  }
}

StatusOr<int> GetSystemTemperature() {
  if (s_sys_temp_sensor == nullptr) return mgos::Status(STATUS_NOT_FOUND, "");
  auto st = s_sys_temp_sensor->GetTemperature();
  if (!st.ok()) return st;
  return static_cast<int>(st.ValueOrDie());
}

uint8_t GetServiceFlags() {
  return s_service_flags;
}

void SetServiceFlags(uint8_t flags) {
  s_service_flags |= flags;
}

void ClearServiceFlags(uint8_t flags) {
  s_service_flags &= ~flags;
}

bool AllComponentsIdle() {
  for (const auto &c : g_comps) {
    if (!c->IsIdle()) return false;
  }
  return true;
}

static void StatusTimerCB(void *arg) {
  static uint8_t s_cnt = 0;
  auto sys_temp = GetSystemTemperature();
  if (mgos_sys_config_get_shelly_legacy_hap_layout() &&
      !HAPAccessoryServerIsPaired(&s_server)) {
    DisableLegacyHAPLayout();
    RestartService();
    return;
  }
  /* If provisioning information has been provided, start the server. */
  StartService(true /* quiet */);
  CheckSysLED();
  if (sys_temp.ok()) {
    CheckOverheat(sys_temp.ValueOrDie());
  }
#if CS_PLATFORM == CS_P_ESP8266
  // If committed, set up inactive app slot as location for core dumps.
  static bool s_cd_area_set = false;
  struct mgos_ota_status ota_status;
  if (!s_cd_area_set && mgos_ota_is_committed() &&
      mgos_ota_get_status(&ota_status)) {
    rboot_config bcfg = rboot_get_config();
    int cd_slot = (ota_status.partition == 0 ? 1 : 0);
    uint32_t cd_addr = bcfg.roms[cd_slot];
    uint32_t cd_size = bcfg.roms_sizes[cd_slot];
    esp_core_dump_set_flash_area(cd_addr, cd_size);
    s_cd_area_set = true;
  }
#endif
  if (++s_cnt % 8 == 0) {
    HAPPlatformTCPStreamManagerStats tcpm_stats = {};
    HAPPlatformTCPStreamManagerGetStats(&s_tcpm, &tcpm_stats);
    int num_sessions = 0;
    HAPAccessoryServerEnumerateConnectedSessions(
        &s_server,
        [](void *ctx, HAPAccessoryServerRef *, HAPSessionRef *, bool *) {
          (*((int *) ctx))++;
        },
        &num_sessions);
    std::string status;
    for (const auto &c : g_comps) {
      if (!status.empty()) status.append("; ");
      status.append(mgos::SPrintf("%d.%d: ", (int) c->type(), c->id()));
      auto sts = c->GetInfo();
      if (sts.ok()) {
        status.append(sts.ValueOrDie());
      } else {
        status.append(sts.status().error_message());
      }
    }
    if (status.empty()) status = "disabled";
    LOG(LL_INFO, ("Up %.2lf, HAP %u/%u/%u ns %d, RAM: %lu/%lu; st %d; %s",
                  mgos_uptime(), (unsigned) tcpm_stats.numPendingTCPStreams,
                  (unsigned) tcpm_stats.numActiveTCPStreams,
                  (unsigned) tcpm_stats.maxNumTCPStreams, num_sessions,
                  (unsigned long) mgos_get_free_heap_size(),
                  (unsigned long) mgos_get_min_free_heap_size(),
                  (sys_temp.ok() ? sys_temp.ValueOrDie() : 0), status.c_str()));
  }
#ifdef MGOS_SYS_CONFIG_HAVE_SHELLY_WIFI_CONNECT_REBOOT_TIMEOUT
  const WifiConfig &wc = GetWifiConfig();
  if ((wc.sta.enable || wc.sta1.enable) &&
      mgos_sys_config_get_shelly_wifi_connect_reboot_timeout() > 0) {
    static int64_t s_last_connected = 0;
    int64_t now = mgos_uptime_micros();
    const WifiInfo &wi = GetWifiInfo();
    if (!wi.sta_connected) {
      s_last_connected = now;
    } else if (AllComponentsIdle()) {  // Only if all components are idle.
      int64_t timeout_micros =
          mgos_sys_config_get_shelly_wifi_connect_reboot_timeout() * 1000000;
      if (now - s_last_connected > timeout_micros) {
        LOG(LL_ERROR, ("Not connected for too long, rebooting"));
        mgos_system_restart_after(500);
      }
    }
  }
#endif  // MGOS_SYS_CONFIG_HAVE_SHELLY_WIFI_CONNECT_REBOOT_TIMEOUT
  (void) arg;
}

#ifndef MGOS_HAVE_WIFI
const char *mgos_sys_config_get_wifi_sta_ssid(void) {
  return "";
}
const char *mgos_sys_config_get_wifi_sta_pass(void) {
  return "";
}
bool mgos_sys_config_get_wifi_sta_enable(void) {
  return false;
}
#endif

static bool MigrateConfig(bool *reboot_required) {
  bool changed = false;
  *reboot_required = false;
  if (mgos_sys_config_get_shelly_cfg_version() == 0) {
    // Very first migration after conversion, reset all settings to defaults
    // except WiFi.
    SanitizeSysConfig();
#ifdef MGOS_CONFIG_HAVE_SW1
    if (mgos_sys_config_get_sw1_persist_state()) {
      mgos_sys_config_set_sw1_initial_state(
          static_cast<int>(InitialState::kLast));
    }
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
    if (mgos_sys_config_get_sw2_persist_state()) {
      mgos_sys_config_set_sw2_initial_state(
          static_cast<int>(InitialState::kLast));
    }
#endif
    mgos_sys_config_set_shelly_cfg_version(1);
    changed = true;
  }
  if (mgos_sys_config_get_shelly_cfg_version() == 1) {
#if defined(MGOS_CONFIG_HAVE_SW1) && defined(MGOS_CONFIG_HAVE_SW2)
    // If already paired, preserve legacy layout.
    // https://github.com/mongoose-os-apps/shelly-homekit/issues/9#issuecomment-694418580
    if (HAPAccessoryServerIsPaired(&s_server) &&
        mgos_sys_config_get_sw1_in_mode() != 3 &&
        mgos_sys_config_get_sw2_in_mode() != 3) {
      mgos_sys_config_set_shelly_legacy_hap_layout(true);
    }
#endif
    mgos_sys_config_set_shelly_cfg_version(2);
    changed = true;
  }
  if (mgos_sys_config_get_shelly_cfg_version() == 2) {
    // Reset device ID to default, to keep it unique.
    // User-specified name will be stored in shelly.name.
    // dns_sd.host_name is kept in sync.
    mgos_sys_config_set_shelly_name(mgos_sys_config_get_device_id());
    mgos_sys_config_set_dns_sd_host_name(mgos_sys_config_get_device_id());
    std::string s(mgos_sys_config_get_default_device_id());
    mgos_expand_mac_address_placeholders(const_cast<char *>(s.c_str()));
    mgos_sys_config_set_device_id(s.c_str());
    mgos_sys_config_set_shelly_cfg_version(3);
    changed = true;
  }
  if (mgos_sys_config_get_shelly_cfg_version() == 3) {
#ifdef MGOS_CONFIG_HAVE_SSW1
    mgos_sys_config_set_in1_ssw_name(mgos_sys_config_get_ssw1_name());
    mgos_sys_config_set_in1_ssw_in_mode(mgos_sys_config_get_ssw1_in_mode());
#endif
#ifdef MGOS_CONFIG_HAVE_SSW2
    mgos_sys_config_set_in2_ssw_name(mgos_sys_config_get_ssw2_name());
    mgos_sys_config_set_in2_ssw_in_mode(mgos_sys_config_get_ssw2_in_mode());
#endif
#ifdef MGOS_CONFIG_HAVE_SSW3
    mgos_sys_config_set_in3_ssw_name(mgos_sys_config_get_ssw3_name());
    mgos_sys_config_set_in3_ssw_in_mode(mgos_sys_config_get_ssw3_in_mode());
#endif
    mgos_sys_config_set_shelly_cfg_version(4);
    changed = true;
  }
  if (mgos_sys_config_get_shelly_cfg_version() == 4) {
    if (HAPAccessoryServerIncrementCN(&s_kvs) != kHAPError_None) {
      LOG(LL_ERROR, ("Failed to increment CN"));
    }
    // Disable file logging.
    if (mgos_sys_config_get_file_logger_enable()) {
      SetDebugEnable(false);
    }
    mgos_sys_config_set_shelly_cfg_version(5);
    changed = true;
  }
  if (mgos_sys_config_get_shelly_cfg_version() == 5) {
    if (mgos_sys_config_get_rpc_acl_file() != nullptr) {
      mgos_sys_config_set_rpc_acl(mgos_sys_config_get_default__const_rpc_acl());
      mgos_sys_config_set_rpc_acl_file(nullptr);
    }
    mgos_sys_config_set_shelly_cfg_version(6);
    changed = true;
  }
  return changed;
}

static void RebootCB(int ev, void *ev_data, void *userdata) {
  s_service_flags |= SHELLY_SERVICE_FLAG_REBOOT;
  if (HAPAccessoryServerGetState(&s_server) ==
      kHAPAccessoryServerState_Running) {
    HAPAccessoryServerStop(&s_server);
  }
  if (ev == MGOS_EVENT_REBOOT &&
      !(s_service_flags & SHELLY_SERVICE_FLAG_REVERT)) {
    // Increment CN on every reboot, because why not.
    // This will cover firmware update as well as other configuration changes.
    if (HAPAccessoryServerIncrementCN(&s_kvs) != kHAPError_None) {
      LOG(LL_ERROR, ("Failed to increment CN"));
    }
  }
  (void) ev;
  (void) ev_data;
  (void) userdata;
}

void RestartService() {
  StopService();
  if (HAPAccessoryServerIncrementCN(&s_kvs) != kHAPError_None) {
    LOG(LL_ERROR, ("Failed to increment configuration number"));
  }
  // Structural change, disable legacy mode if enabled.
  DisableLegacyHAPLayout();
  // Server will be restarted by status timer (unless inhibited).
}

extern "C" bool mgos_ota_merge_fs_should_copy_file(const char *old_fs_path,
                                                   const char *new_fs_path,
                                                   const char *file_name) {
  static const char *s_skip_files[] = {
      // Some files from stock that we don't need.
      "cert.pem",
      "passwd",
      "relaydata",
      "index.html",
      "conf9_backup.json",
      // Obsolete files from previous versions.
      "axios.min.js.gz",
      "favicon.ico",
      "logo.png",
      "rpc_acl.json",
      "style.css",
      "style.css.gz",
      // Plus firmware stuff that we don't need.
      "api_math.js",
      "api_rpc.js",
      "bundle.css.gz",
      "bundle.js.gz",
      "ca.pem",
      "index.html",
      "init.js",
      "rpc_acl_auth.json",
      "rpc_acl_no_auth.json",
      "storage.json",
      "tzinfo",
  };
  for (const char *skip_fn : s_skip_files) {
    if (strcmp(file_name, skip_fn) == 0) return false;
  }
  struct stat st;
  char buf[100] = {};
  snprintf(buf, sizeof(buf) - 1, "%s/%s", new_fs_path, file_name);
  (void) old_fs_path;
  /* Copy if the file is not found on the new fs. */
  return (stat(buf, &st) != 0);
}

static void HTTPHandler(struct mg_connection *nc, int ev, void *ev_data,
                        void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  struct http_message *hm = (struct http_message *) ev_data;
  const char *file = nullptr, *type = nullptr;
  if (mg_vcasecmp(&hm->method, "GET") == 0) {
    if (mg_vcmp(&hm->uri, "/") == 0 || mg_vcmp(&hm->uri, "/ota") == 0) {
      file = "index.html.gz";
      type = "text/html";
    } else if (mg_vcmp(&hm->uri, "/favicon.ico") == 0) {
      file = "favicon.ico.gz";
      type = "image/x-icon";
    }
  }
  if (file == nullptr) {
    mg_http_send_error(nc, 404, nullptr);
    nc->flags |= MG_F_SEND_AND_CLOSE;
    return;
  }
  {
    char addr[32] = {};
    mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP);
    ReportClientRequest(addr);
  }
  mg_http_serve_file(nc, hm, file, mg_mk_str(type),
                     mg_mk_str("Content-Encoding: gzip\r\nPragma: no-cache"));
  (void) user_data;
}

mgos::hap::Accessory::IdentifyCB GetIdentifyCB() {
  return s_identify_cb;
}

void SetIdentifyCB(mgos::hap::Accessory::IdentifyCB cb) {
  s_identify_cb = cb;
}

void InitApp() {
  struct mg_http_endpoint_opts opts = {};
  mgos_register_http_endpoint_opt("/", HTTPHandler, opts);
  // Support /ota?url=... updates a-la stock.
  mgos_register_http_endpoint_opt("/ota", HTTPHandler, opts);

  if (IsFailsafeMode()) {
    LOG(LL_INFO, ("== Failsafe mode, not initializing the app"));
    RPCServiceInit(nullptr, nullptr, nullptr);
    if (LED_GPIO >= 0) {
      mgos_gpio_setup_output(LED_GPIO, LED_ON);
    }
    return;
  }

  InitWifiConfigManager();

  CheckRebootCounter();

  // Key-value store.
  static const HAPPlatformKeyValueStoreOptions kvs_opts = {
      .fileName = KVS_FILE_NAME,
  };
  HAPPlatformKeyValueStoreCreate(&s_kvs, &kvs_opts);

  // Accessory setup.
  static const HAPPlatformAccessorySetupOptions as_opts = {};
  HAPPlatformAccessorySetupCreate(&s_accessory_setup, &as_opts);

  // TCP Stream Manager.
  static const HAPPlatformTCPStreamManagerOptions tcpm_opts = {
      .port = kHAPNetworkPort_Any,
      .maxConcurrentTCPStreams = MAX_NUM_HAP_SESSIONS,
  };
  HAPPlatformTCPStreamManagerCreate(&s_tcpm, &tcpm_opts);

  // Service discovery.
  static const HAPPlatformServiceDiscoveryOptions sd_opts = {};
  HAPPlatformServiceDiscoveryCreate(&s_service_discovery, &sd_opts);

  s_callbacks.handleUpdatedState = HAPServerStateUpdateCB;

  // Initialize accessory server.
  HAPAccessoryServerOptions server_options = {
    .maxPairings = kHAPPairingStorage_MinElements,
#if HAP_IP
    .ip =
        {
            .transport = &kHAPAccessoryServerTransport_IP,
#ifndef __clang__
            .available = 0,
#endif
            .accessoryServerStorage = &s_ip_storage,
        },
#endif
#if HAP_BLE
    .ble =
        {
            .transport = nullptr,
#ifndef __clang__
            .available = 0,
#endif
            .accessoryServerStorage = nullptr,
            .preferredAdvertisingInterval = 0,
            .preferredNotificationDuration = 0,
        },
#endif
  };
  static struct HAPPlatformMFiTokenAuth s_mfi_auth;
  HAPPlatformMFiTokenAuthOptions mfi_opts = {};
  HAPPlatformMFiTokenAuthCreate(&s_mfi_auth, &mfi_opts);
  HAPPlatform platform = {
      .keyValueStore = &s_kvs,
      .accessorySetup = &s_accessory_setup,
      .setupDisplay = nullptr,
      .setupNFC = nullptr,
      .ip =
          {
              .tcpStreamManager = &s_tcpm,
              .serviceDiscovery = &s_service_discovery,
          },
      .ble =
          {
              .blePeripheralManager = nullptr,
          },
      .authentication =
          {
              .mfiHWAuth = nullptr,
              .mfiTokenAuth = &s_mfi_auth,
          },
  };
#ifdef MGOS_HAVE_BT_COMMON
  // BLE Preipheral Manager.
  if (mgos_sys_config_get_bt_enable()) {
    static HAPPlatformBLEPeripheralManagerOptions blepm_opts = {};
    HAPPlatformBLEPeripheralManagerCreate(&s_blepm, &blepm_opts);
    platform.ble.blePeripheralManager = &s_blepm;
    server_options.ble.transport = &kHAPAccessoryServerTransport_BLE;
    server_options.ble.accessoryServerStorage = &s_ble_storage;
    server_options.ble.preferredAdvertisingInterval =
        HAPBLEAdvertisingIntervalCreateFromMilliseconds(417.5f);
    server_options.ble.preferredNotificationDuration =
        kHAPBLENotification_MinDuration;
  }
#endif
  HAPAccessoryServerCreate(&s_server, &server_options, &platform, &s_callbacks,
                           nullptr /* context */);

  bool reboot_required = false;
  if (MigrateConfig(&reboot_required)) {
    mgos_sys_config_save(&mgos_sys_config, false /* try_once */,
                         nullptr /* msg */);
    if (reboot_required) {
      mgos_system_restart_after(500);
      LOG(LL_INFO, ("Configuration change requires %s", "reboot"));
      return;
    }
  }

  LOG(LL_INFO, ("=== Creating peripherals"));
  CreatePeripherals(&s_inputs, &s_outputs, &s_pms, &s_sys_temp_sensor);
  if (s_sys_temp_sensor) {
    Status st = s_sys_temp_sensor->Init();
    if (!st.ok()) {
      LOG(LL_ERROR, ("Sys temp sensor init failed: %s", st.ToString().c_str()));
    }
  }

  StartService(false /* quiet */);

  // House-keeping timer.
  mgos_set_timer(1000, MGOS_TIMER_REPEAT, StatusTimerCB, nullptr);

  mgos_hap_add_rpc_service_cb(
      &s_server,
      [](HAPAccessoryServerRef *) { HAPAccessoryServerStop(&s_server); },
      [](HAPAccessoryServerRef *) { StartService(false /* quiet */); });

  RPCServiceInit(&s_server, &s_kvs, &s_tcpm);

  DebugInit(&s_server, &s_kvs, &s_tcpm);

  mgos_event_add_handler(MGOS_EVENT_REBOOT, RebootCB, nullptr);
  mgos_event_add_handler(MGOS_EVENT_REBOOT_AFTER, RebootCB, nullptr);

  StartWifiConfigManager();

  OTAInit(&s_server);

  (void) s_ip_storage;
}

}  // namespace shelly

extern "C" enum mgos_app_init_result mgos_app_init(void) {
  shelly::InitApp();
  return MGOS_APP_INIT_SUCCESS;
}
