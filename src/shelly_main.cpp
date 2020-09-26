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

#include "shelly_main.hpp"

#include "mgos.hpp"
#include "mgos_app.h"
#include "mgos_hap.h"
#ifdef MGOS_HAVE_OTA_COMMON
#include "esp_coredump.h"
#include "esp_rboot.h"
#include "mgos_ota.h"
#endif
#include "mgos_rpc.h"

#include "HAP.h"
#include "HAPAccessoryServer+Internal.h"
#include "HAPPlatform+Init.h"
#include "HAPPlatformAccessorySetup+Init.h"
#include "HAPPlatformKeyValueStore+Init.h"
#include "HAPPlatformServiceDiscovery+Init.h"
#include "HAPPlatformTCPStreamManager+Init.h"

#include "shelly_debug.hpp"
#include "shelly_hap_lock.hpp"
#include "shelly_hap_outlet.hpp"
#include "shelly_hap_stateless_switch.hpp"
#include "shelly_hap_switch.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"
#include "shelly_rpc_service.hpp"

#define KVS_FILE_NAME "kvs.json"
#define NUM_SESSIONS 9
#define SCRATCH_BUF_SIZE 1536

#ifndef LED_ON
#define LED_ON 0
#endif
#ifndef BTN_DOWN
#define BTN_DOWN 0
#endif

namespace shelly {

static HAPIPSession sessions[NUM_SESSIONS];
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

static HAPPlatformKeyValueStore s_kvs;
static HAPPlatformAccessorySetup s_accessory_setup;
static HAPAccessoryServerOptions s_server_options = {
    .maxPairings = kHAPPairingStorage_MinElements,
    .ip =
        {
            .transport = &kHAPAccessoryServerTransport_IP,
#ifndef __clang__
            .available = 0,
#endif
            .accessoryServerStorage = &s_ip_storage,
        },
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
};
static HAPAccessoryServerCallbacks s_callbacks;
static HAPPlatformTCPStreamManager s_tcpm;
static HAPPlatformServiceDiscovery s_service_discovery;
static HAPAccessoryServerRef s_server;

static HAPPlatform s_platform = {
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
            .mfiTokenAuth = nullptr,
        },
};

static bool s_hap_enable = true;
static bool s_recreate_accs = false;
static int16_t s_btn_pressed_count = 0;
static int16_t s_identify_count = 0;

static void CheckLED(int pin, bool led_act);

HAPError AccessoryIdentifyCB(const HAPAccessoryIdentifyRequest *request) {
  LOG(LL_INFO, ("=== IDENTIFY ==="));
  s_identify_count = 3;
  CheckLED(LED_GPIO, LED_ON);
  (void) request;
  return kHAPError_None;
}

std::vector<Component *> g_comps;
static std::vector<std::unique_ptr<Input>> s_inputs;
static std::vector<std::unique_ptr<Output>> s_outputs;
static std::vector<std::unique_ptr<PowerMeter>> s_pms;
static std::vector<std::unique_ptr<hap::Accessory>> s_accs;
static std::vector<const HAPAccessory *> s_hap_accs;

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

static void DoReset(void *arg) {
  intptr_t out_gpio = (intptr_t) arg;
  if (out_gpio >= 0) {
    mgos_gpio_blink(out_gpio, 0, 0);
  }
  LOG(LL_INFO, ("Performing reset"));
#ifdef MGOS_SYS_CONFIG_HAVE_WIFI
  mgos_sys_config_set_wifi_sta_enable(false);
  mgos_sys_config_set_wifi_ap_enable(true);
  mgos_sys_config_save(&mgos_sys_config, false, nullptr);
  mgos_wifi_setup((struct mgos_config_wifi *) mgos_sys_config_get_wifi());
#endif
}

void HandleInputResetSequence(InputPin *in, int out_gpio, Input::Event ev,
                              bool cur_state) {
  if (ev != Input::Event::kReset) return;
  LOG(LL_INFO, ("%d: Reset sequence detected", in->id()));
  if (out_gpio >= 0) {
    mgos_gpio_blink(out_gpio, 100, 100);
  }
  mgos_set_timer(600, 0, DoReset, (void *) (intptr_t) out_gpio);
  (void) cur_state;
}

void CreateHAPSwitch(int id, const struct mgos_config_sw *sw_cfg,
                     const struct mgos_config_ssw *ssw_cfg,
                     std::vector<Component *> *comps,
                     std::vector<std::unique_ptr<hap::Accessory>> *accs,
                     HAPAccessoryServerRef *svr, bool to_pri_acc) {
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
      sw.reset(new hap::Switch(id, in, out, pm, cfg));
      break;
    case 1:
      cat = kHAPAccessoryCategory_Outlets;
      aid = SHELLY_HAP_AID_BASE_OUTLET + id;
      sw.reset(new hap::Outlet(id, in, out, pm, cfg));
      break;
    case 2:
      cat = kHAPAccessoryCategory_Locks;
      aid = SHELLY_HAP_AID_BASE_LOCK + id;
      sw.reset(new hap::Lock(id, in, out, pm, cfg));
      break;
    default:
      sw.reset(new ShellySwitch(id, in, out, pm, cfg));
      sw_hidden = true;
      break;
  }
  auto st = sw->Init();
  if (!st.ok()) {
    const std::string &s = st.ToString();
    LOG(LL_ERROR, ("Error creating switch: %s", s.c_str()));
    return;
  }
  comps->push_back(sw.get());
  hap::Accessory *pri_acc = accs->front().get();
  if (to_pri_acc) {
    // NB: this produces duplicate primary services on multi-switch devices in
    // legacy mode. This is necessary to ensure accessory configuration remains
    // exactly the same.
    sw->set_primary(true);
    pri_acc->SetCategory(cat);
    pri_acc->AddService(std::move(sw));
    return;
  }
  if (!sw_hidden) {
    std::unique_ptr<hap::Accessory> acc(
        new hap::Accessory(aid, kHAPAccessoryCategory_BridgedAccessory,
                           sw_cfg->name, &AccessoryIdentifyCB, svr));
    acc->AddHAPService(&mgos_hap_accessory_information_service);
    acc->AddService(std::move(sw));
    accs->push_back(std::move(acc));
  } else {
    // This one will not be exported so shove it into primary,
    // purely for ownership.
    pri_acc->AddService(std::move(sw));
  }
  if (sw_cfg->in_mode == 3) {
    LOG(LL_INFO, ("Creating a stateless switch for input %d", id));
    CreateHAPStatelessSwitch(id, ssw_cfg, comps, accs, svr);
  }
}

void CreateHAPStatelessSwitch(
    int id, const struct mgos_config_ssw *ssw_cfg,
    std::vector<Component *> *comps,
    std::vector<std::unique_ptr<hap::Accessory>> *accs,
    HAPAccessoryServerRef *svr) {
  std::unique_ptr<hap::StatelessSwitch> ssw(new hap::StatelessSwitch(
      id, FindInput(id), (struct mgos_config_ssw *) ssw_cfg, 0));
  if (ssw == nullptr || !ssw->Init().ok()) {
    return;
  }
  comps->push_back(ssw.get());
  std::unique_ptr<hap::Accessory> acc(
      new hap::Accessory(SHELLY_HAP_AID_BASE_STATELESS_SWITCH + id,
                         kHAPAccessoryCategory_BridgedAccessory, ssw_cfg->name,
                         &AccessoryIdentifyCB, svr));
  acc->AddHAPService(&mgos_hap_accessory_information_service);
  acc->AddService(std::move(ssw));
  accs->push_back(std::move(acc));
}

static void DisableLegacyHAPLayout() {
  if (!mgos_sys_config_get_shelly_legacy_hap_layout()) return;
  LOG(LL_INFO, ("Turning off legacy HAP layout"));
  mgos_sys_config_set_shelly_legacy_hap_layout(false);
  mgos_sys_config_save(&mgos_sys_config, false /* try_once */, nullptr);
}

static bool StartHAPServer(bool quiet) {
  if (!s_hap_enable) {
    return false;
  }
  if (HAPAccessoryServerGetState(&s_server) != kHAPAccessoryServerState_Idle) {
    return true;
  }
  if (!s_accs.empty() && s_recreate_accs) {
    LOG(LL_INFO, ("=== Reconfiguring accessories"));
    s_accs.clear();
    s_hap_accs.clear();
    g_comps.clear();
    s_recreate_accs = false;
    if (HAPAccessoryServerIncrementCN(&s_kvs) != kHAPError_None) {
      LOG(LL_ERROR, ("Failed to increment configuration number"));
    }
    // Structural change, disable legacy mode if enabled.
    DisableLegacyHAPLayout();
  }
  if (s_accs.empty()) {
    LOG(LL_INFO, ("=== Creating accessories"));
    std::unique_ptr<hap::Accessory> pri_acc(new hap::Accessory(
        SHELLY_HAP_AID_PRIMARY, kHAPAccessoryCategory_Bridges,
        mgos_sys_config_get_device_id(), &AccessoryIdentifyCB, &s_server));
    pri_acc->AddHAPService(&mgos_hap_accessory_information_service);
    pri_acc->AddHAPService(&mgos_hap_protocol_information_service);
    pri_acc->AddHAPService(&mgos_hap_pairing_service);
    s_accs.push_back(std::move(pri_acc));
    CreateComponents(&g_comps, &s_accs, &s_server);
    s_accs.shrink_to_fit();
    g_comps.shrink_to_fit();
  }

  if (!mgos_hap_config_valid()) {
    if (!quiet) {
      LOG(LL_INFO, ("=== Accessory not provisioned"));
    }
    return false;
  }
  uint16_t cn;
  if (HAPAccessoryServerGetCN(&s_kvs, &cn) != kHAPError_None) {
    cn = 0;
  }
  if (s_accs.size() == 1) {
    LOG(LL_INFO, ("=== Starting HAP %s (CN %d)", "server", cn));
    HAPAccessoryServerStart(&s_server, s_accs.front()->GetHAPAccessory());
  } else {
    if (s_hap_accs.empty()) {
      for (auto it = s_accs.begin() + 1; it != s_accs.end(); it++) {
        s_hap_accs.push_back((*it)->GetHAPAccessory());
      }
      s_hap_accs.push_back(nullptr);
      s_hap_accs.shrink_to_fit();
    }
    LOG(LL_INFO, ("=== Starting HAP %s (CN %d, %d accessories)", "bridge", cn,
                  (int) s_hap_accs.size()));
    HAPAccessoryServerStartBridge(&s_server, s_accs.front()->GetHAPAccessory(),
                                  s_hap_accs.data(),
                                  false /* config changed */);
  }
  return true;
}

static void StartHAPServerCB(HAPAccessoryServerRef *server) {
  StartHAPServer(false /* quiet */);
  (void) server;
}

static void HAPServerStateUpdateCB(HAPAccessoryServerRef *server,
                                   void *context) {
  LOG(LL_INFO, ("HAP server state: %d", HAPAccessoryServerGetState(server)));
  (void) context;
}

static void CheckButton(int pin, bool btn_down) {
  if (pin < 0) return;
  bool pressed = (mgos_gpio_read(pin) == btn_down);
  if (!pressed) {
    if (s_btn_pressed_count > 0) {
      s_btn_pressed_count = 0;
    }
    return;
  }
  s_btn_pressed_count++;
  LOG(LL_INFO, ("Button pressed, %d", s_btn_pressed_count));
  if (s_btn_pressed_count == 10) {
    DoReset((void *) -1);
  }
}

static void CheckLED(int pin, bool led_act) {
  if (pin < 0) return;
  int on_ms = 0, off_ms = 0;
  static int s_on_ms = 0, s_off_ms = 0;
  // If user is currently holding the button, acknowledge it.
  if (s_btn_pressed_count > 0 && s_btn_pressed_count < 10) {
    LOG(LL_DEBUG, ("LED: btn (%d)", s_btn_pressed_count));
    on_ms = 1;
    off_ms = 0;
    goto out;
  }
  // Identify sequence requested by controller.
  if (s_identify_count > 0) {
    LOG(LL_DEBUG, ("LED: identify (%d)", s_identify_count));
    on_ms = 100;
    off_ms = 100;
    s_identify_count--;
    goto out;
  }
#ifdef MGOS_HAVE_WIFI
  // Are we connecting to wifi right now?
  switch (mgos_wifi_get_status()) {
    case MGOS_WIFI_CONNECTING:
    case MGOS_WIFI_CONNECTED:
      LOG(LL_DEBUG, ("LED: WiFi"));
      on_ms = 200;
      off_ms = 200;
      goto out;
    default:
      break;
  }
#endif
#ifdef MGOS_HAVE_OTA_COMMON
  if (mgos_ota_is_in_progress()) {
    LOG(LL_DEBUG, ("LED: OTA"));
    on_ms = 250;
    off_ms = 250;
    goto out;
  }
#endif
  // HAP server status (if WiFi is provisioned).
  if (HAPAccessoryServerGetState(&s_server) !=
      kHAPAccessoryServerState_Running) {
    off_ms = 875;
    on_ms = 25;
    LOG(LL_DEBUG, ("LED: HAP provisioning"));
  } else {
#ifdef MGOS_HAVE_WIFI
    // Indicate WiFi provisioning status.
    if (mgos_sys_config_get_wifi_ap_enable()) {
      LOG(LL_DEBUG, ("LED: WiFi provisioning"));
      off_ms = 25;
      on_ms = 875;
    }
#endif
    if (on_ms == 0 && !HAPAccessoryServerIsPaired(&s_server)) {
      LOG(LL_DEBUG, ("LED: Pairing"));
      off_ms = 500;
      on_ms = 500;
    }
  }
out:
  if (on_ms > 0) {
    if (on_ms > 1) {
      mgos_gpio_set_mode(pin, MGOS_GPIO_MODE_OUTPUT);
      if (on_ms != s_on_ms || off_ms != s_off_ms) {
        if (led_act) {
          mgos_gpio_blink(pin, on_ms, off_ms);
        } else {
          mgos_gpio_blink(pin, off_ms, on_ms);
        }
        s_on_ms = on_ms;
        s_off_ms = off_ms;
      }
    } else {
      s_on_ms = s_off_ms = 0;
      mgos_gpio_blink(pin, 0, 0);
      mgos_gpio_setup_output(pin, led_act);
    }
  } else {
    mgos_gpio_set_mode(pin, MGOS_GPIO_MODE_INPUT);
  }
}

static void StatusTimerCB(void *arg) {
  static uint8_t s_cnt = 0;
  if (++s_cnt % 8 == 0) {
    HAPPlatformTCPStreamManagerStats tcpm_stats = {};
    HAPPlatformTCPStreamManagerGetStats(&s_tcpm, &tcpm_stats);
    LOG(LL_INFO, ("Uptime: %.2lf, conns %u/%u/%u, RAM: %lu, %lu free",
                  mgos_uptime(), (unsigned) tcpm_stats.numPendingTCPStreams,
                  (unsigned) tcpm_stats.numActiveTCPStreams,
                  (unsigned) tcpm_stats.maxNumTCPStreams,
                  (unsigned long) mgos_get_heap_size(),
                  (unsigned long) mgos_get_free_heap_size()));
    s_cnt = 0;
  }
  if (mgos_sys_config_get_shelly_legacy_hap_layout() &&
      !HAPAccessoryServerIsPaired(&s_server)) {
    DisableLegacyHAPLayout();
    RestartHAPServer();
    return;
  }
  /* If provisioning information has been provided, start the server. */
  StartHAPServer(true /* quiet */);
  CheckButton(BTN_GPIO, BTN_DOWN);
  CheckLED(LED_GPIO, LED_ON);
#ifdef MGOS_HAVE_OTA_COMMON
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

static bool shelly_cfg_migrate(void) {
  bool changed = false;
  if (mgos_sys_config_get_shelly_cfg_version() == 0) {
#ifdef MGOS_CONFIG_HAVE_SW1
    if (mgos_sys_config_get_sw1_persist_state()) {
      mgos_sys_config_set_sw1_initial_state(
          static_cast<int>(ShellySwitch::InitialState::kLast));
    }
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
    if (mgos_sys_config_get_sw2_persist_state()) {
      mgos_sys_config_set_sw2_initial_state(
          static_cast<int>(ShellySwitch::InitialState::kLast));
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
  return changed;
}

static void RebootCB(int ev, void *ev_data, void *userdata) {
  s_hap_enable = false;
  if (HAPAccessoryServerGetState(&s_server) ==
      kHAPAccessoryServerState_Running) {
    HAPAccessoryServerStop(&s_server);
  }
  if (ev == MGOS_EVENT_REBOOT) {
    // Increment CN on every reboot, because why not.
    // This will cover firmware update as well as other configuration changes.
    uint16_t cn;
    if (HAPAccessoryServerGetCN(&s_kvs, &cn) != kHAPError_None) {
      cn = 0;
    }
  }
  (void) ev;
  (void) ev_data;
  (void) userdata;
}

void RestartHAPServer() {
  s_recreate_accs = true;
  if (HAPAccessoryServerGetState(&s_server) ==
      kHAPAccessoryServerState_Running) {
    HAPAccessoryServerStop(&s_server);
  }
}

bool InitApp() {
#ifdef MGOS_HAVE_OTA_COMMON
  if (mgos_ota_is_first_boot()) {
    LOG(LL_INFO, ("Performing cleanup"));
    // In case we're uograding from stock fw, remove its files
    // with the exception of hwinfo_struct.json.
    remove("cert.pem");
    remove("passwd");
    remove("relaydata");
    remove("index.html");
    remove("style.css");
  }
#endif

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
      .maxConcurrentTCPStreams = NUM_SESSIONS,
  };
  HAPPlatformTCPStreamManagerCreate(&s_tcpm, &tcpm_opts);

  // Service discovery.
  static const HAPPlatformServiceDiscoveryOptions sd_opts = {};
  HAPPlatformServiceDiscoveryCreate(&s_service_discovery, &sd_opts);

  s_callbacks.handleUpdatedState = HAPServerStateUpdateCB;

  // Initialize accessory server.
  HAPAccessoryServerCreate(&s_server, &s_server_options, &s_platform,
                           &s_callbacks, nullptr /* context */);

  if (shelly_cfg_migrate()) {
    mgos_sys_config_save(&mgos_sys_config, false /* try_once */,
                         nullptr /* msg */);
  }

  CreatePeripherals(&s_inputs, &s_outputs, &s_pms);

  StartHAPServer(false /* quiet */);

  // House-keeping timer.
  mgos_set_timer(1000, MGOS_TIMER_REPEAT, StatusTimerCB, nullptr);

  mgos_hap_add_rpc_service_cb(&s_server, StartHAPServerCB);

  if (BTN_GPIO >= 0) {
    mgos_gpio_setup_input(BTN_GPIO, MGOS_GPIO_PULL_UP);
  }

  shelly_rpc_service_init(&s_server, &s_kvs, &s_tcpm);

  shelly_debug_init(&s_kvs, &s_tcpm);

  mgos_event_add_handler(MGOS_EVENT_REBOOT, RebootCB, nullptr);
  mgos_event_add_handler(MGOS_EVENT_REBOOT_AFTER, RebootCB, nullptr);

  return true;
}

}  // namespace shelly

extern "C" {
enum mgos_app_init_result mgos_app_init(void) {
  return (shelly::InitApp() ? MGOS_APP_INIT_SUCCESS : MGOS_APP_INIT_ERROR);
}
}
