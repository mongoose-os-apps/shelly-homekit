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

#include "mgos.h"
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

static bool s_recreate_acc = false;
static int16_t s_btn_pressed_count = 0;
static int16_t s_identify_count = 0;

static void CheckLED(int pin, bool led_act);

static HAPError IdentifyCB(const HAPAccessoryIdentifyRequest *request) {
  LOG(LL_INFO, ("=== IDENTIFY ==="));
  s_identify_count = 3;
  CheckLED(LED_GPIO, LED_ON);
  (void) request;
  return kHAPError_None;
}

static std::vector<std::unique_ptr<Input>> s_inputs;
static std::vector<std::unique_ptr<Output>> s_outputs;
static std::vector<std::unique_ptr<PowerMeter>> s_pms;
std::vector<std::unique_ptr<Component>> g_components;
static std::unique_ptr<hap::Accessory> s_acc;

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
                     std::vector<std::unique_ptr<Component>> *components,
                     hap::Accessory *acc, hap::ServiceLabelService *sls,
                     HAPAccessoryServerRef *svr) {
  std::unique_ptr<ShellySwitch> sw;
  Input *in = FindInput(id);
  Output *out = FindOutput(id);
  PowerMeter *pm = FindPM(id);
  const HAPAccessory *hap_acc = acc->GetHAPAccessory();
  struct mgos_config_sw *cfg = (struct mgos_config_sw *) sw_cfg;
  switch (sw_cfg->svc_type) {
    case 0:
      sw.reset(new hap::Switch(id, in, out, pm, cfg, svr, hap_acc));
      break;
    case 1:
      sw.reset(new hap::Outlet(id, in, out, pm, cfg, svr, hap_acc));
      break;
    case 2:
      sw.reset(new hap::Lock(id, in, out, pm, cfg, svr, hap_acc));
      break;
    default:
      sw.reset(new ShellySwitch(id, in, out, pm, cfg, svr, hap_acc));
      break;
  }
  auto st = sw->Init();
  if (!st.ok()) {
    sw.reset();
  }

  if (sw != nullptr) {
    acc->AddHAPService(sw->GetHAPService());
    components->push_back(std::move(sw));
  }
  if (sw_cfg->in_mode == (int) ShellySwitch::InMode::kDetached) {
    LOG(LL_INFO, ("Creating a stateless switch for input %d", id));
    std::unique_ptr<hap::StatelessSwitch> ssw(new hap::StatelessSwitch(
        id, FindInput(id), (struct mgos_config_ssw *) ssw_cfg, svr, hap_acc,
        sls->iid()));
    if (ssw != nullptr && ssw->Init().ok()) {
      sls->AddLink(ssw->iid());
      acc->AddHAPService(ssw->GetHAPService());
      components->push_back(std::move(ssw));
    }
  }
}

std::unique_ptr<hap::Accessory> CreateHAPAccessories() {
  std::unique_ptr<hap::Accessory> acc(
      new hap::Accessory(HAP_AID_PRIMARY, kHAPAccessoryCategory_Switches,
                         mgos_sys_config_get_device_id(), &IdentifyCB));
  acc->AddHAPService(&mgos_hap_accessory_information_service);
  acc->AddHAPService(&mgos_hap_protocol_information_service);
  acc->AddHAPService(&mgos_hap_pairing_service);
  std::unique_ptr<hap::ServiceLabelService> sls(
      new hap::ServiceLabelService(1 /* numerals */));
  CreateComponents(&g_components, acc.get(), sls.get(), &s_server);
  acc->AddService(std::move(sls));
  return acc;
}

static bool StartHAPServer(bool quiet) {
  if (HAPAccessoryServerGetState(&s_server) != kHAPAccessoryServerState_Idle) {
    return true;
  }
  if (s_acc != nullptr && s_recreate_acc) {
    LOG(LL_INFO, ("=== Reconfiguring accessories"));
    s_acc.reset();
    g_components.clear();
    s_recreate_acc = false;
    if (HAPAccessoryServerIncrementCN(&s_kvs) != kHAPError_None) {
      LOG(LL_ERROR, ("Failed to increment configuration number"));
    }
  }
  if (s_acc == nullptr) {
    LOG(LL_INFO, ("=== Creating accessories"));
    s_acc = CreateHAPAccessories();
  }
  if (!mgos_hap_config_valid()) {
    if (!quiet) {
      LOG(LL_INFO, ("=== Accessory not provisioned"));
    }
    return false;
  }
  uint16_t cn;
  if (HAPAccessoryServerGetCN(&s_kvs, &cn) == kHAPError_None) {
    LOG(LL_INFO, ("=== Starting HAP server (CN %d)", cn));
  }
  HAPAccessoryServerStart(&s_server, s_acc->GetHAPAccessory());
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
  return changed;
}

static void RebootCB(int ev, void *ev_data, void *userdata) {
  // Increment CN on every reboot, because why not.
  // This will cover firmware update as well as other configuration changes.
  if (HAPAccessoryServerIncrementCN(&s_kvs) != kHAPError_None) {
    LOG(LL_ERROR, ("Failed to increment configuration number"));
  }
  uint16_t cn;
  if (HAPAccessoryServerGetCN(&s_kvs, &cn) == kHAPError_None) {
    LOG(LL_INFO, ("New CN: %d", cn));
  }
  (void) ev;
  (void) ev_data;
  (void) userdata;
}

void RestartHAPServer() {
  s_recreate_acc = true;
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

  if (shelly_cfg_migrate()) {
    mgos_sys_config_save(&mgos_sys_config, false /* try_once */,
                         nullptr /* msg */);
  }

  CreatePeripherals(&s_inputs, &s_outputs, &s_pms);

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

  return true;
}

}  // namespace shelly

extern "C" {
enum mgos_app_init_result mgos_app_init(void) {
  return (shelly::InitApp() ? MGOS_APP_INIT_SUCCESS : MGOS_APP_INIT_ERROR);
}
}
