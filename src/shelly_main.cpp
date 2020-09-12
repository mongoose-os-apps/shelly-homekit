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

#include "shelly_debug.h"
#include "shelly_hap_switch.h"
#include "shelly_input.h"
#include "shelly_output.h"
#include "shelly_rpc_service.h"

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

static bool s_recreate_services = false;
static int16_t s_btn_pressed_count = 0;
static int16_t s_identify_count = 0;

static void CheckLED(int pin, bool led_act);

HAPError IdentifyCB(HAPAccessoryServerRef *server,
                    const HAPAccessoryIdentifyRequest *request, void *context) {
  LOG(LL_INFO, ("=== IDENTIFY ==="));
  s_identify_count = 3;
  CheckLED(LED_GPIO, LED_ON);
  (void) server;
  (void) request;
  (void) context;
  return kHAPError_None;
}

static HAPAccessory s_accessory = {
    .aid = 1,
    .category = kHAPAccessoryCategory_Switches,
    .name = nullptr,  // Set from config,
    .manufacturer = CS_STRINGIFY_MACRO(PRODUCT_VENDOR),
    .model = CS_STRINGIFY_MACRO(PRODUCT_MODEL),
    .serialNumber = nullptr,     // Set from config.
    .firmwareVersion = nullptr,  // Set from build_id.
    .hardwareVersion = CS_STRINGIFY_MACRO(PRODUCT_HW_REV),
    .services = nullptr,  // Set later
    .callbacks = {.identify = IdentifyCB},
};

static std::vector<std::unique_ptr<Input>> s_inputs;
static std::vector<std::unique_ptr<Output>> s_outputs;
static std::vector<std::unique_ptr<PowerMeter>> s_pms;
std::vector<std::unique_ptr<Component>> g_components;

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

static void HandleInputResetSequence(InputPin *in, Input::Event ev,
                                     bool cur_state) {
  if (ev != Input::Event::kReset) return;
  LOG(LL_INFO, ("%d: Reset sequence detected", in->id()));
  intptr_t out_gpio = -1;
  switch (in->id()) {
#ifdef MGOS_CONFIG_HAVE_SW1
    case 1:
      out_gpio = mgos_sys_config_get_sw1_out_gpio();
      break;
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
    case 2:
      out_gpio = mgos_sys_config_get_sw2_out_gpio();
      break;
#endif
  }
  if (out_gpio >= 0) {
    mgos_gpio_blink(out_gpio, 100, 100);
  }
  mgos_set_timer(600, 0, DoReset, (void *) out_gpio);
  (void) cur_state;
}

const HAPService **CreateHAPServices() {
  const HAPService **services =
      (const HAPService **) calloc(3 + NUM_SWITCHES + 1, sizeof(*services));
  services[0] = &mgos_hap_accessory_information_service;
  services[1] = &mgos_hap_protocol_information_service;
  services[2] = &mgos_hap_pairing_service;
  int i = 3;
#ifdef MGOS_CONFIG_HAVE_SW1
  {
    auto *sw1_cfg = (struct mgos_config_sw *) mgos_sys_config_get_sw1();
    std::unique_ptr<HAPSwitch> sw1(new HAPSwitch(FindInput(1), FindOutput(1),
                                                 FindPM(1), sw1_cfg, &s_server,
                                                 &s_accessory));
    if (sw1 != nullptr && sw1->Init().ok()) {
      const HAPService *svc = sw1->GetHAPService();
      if (svc != nullptr) services[i++] = svc;
      g_components.push_back(std::move(sw1));
    }
  }
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
  {
    auto *sw2_cfg = (struct mgos_config_sw *) mgos_sys_config_get_sw2();
    std::unique_ptr<HAPSwitch> sw2(new HAPSwitch(FindInput(2), FindOutput(2),
                                                 FindPM(2), sw2_cfg, &s_server,
                                                 &s_accessory));
    if (sw2 != nullptr && sw2->Init().ok()) {
      const HAPService *svc = sw2->GetHAPService();
      if (svc != nullptr) services[i++] = svc;
      g_components.push_back(std::move(sw2));
    }
  }
#endif
  (void) HandleInputResetSequence;
  LOG(LL_INFO, ("Exported %d of %d switches", i - 3, NUM_SWITCHES));
  return services;
}

static bool StartHAPServer(bool quiet) {
  if (HAPAccessoryServerGetState(&s_server) != kHAPAccessoryServerState_Idle) {
    return true;
  }
  if (!mgos_hap_config_valid()) {
    if (!quiet) {
      LOG(LL_INFO, ("=== Accessory not provisioned"));
    }
  }
  if (s_accessory.services != nullptr && s_recreate_services) {
    LOG(LL_INFO, ("=== Re-creating services"));
    free((void *) s_accessory.services);
    s_accessory.services = nullptr;
    g_components.clear();
    s_recreate_services = false;
    if (HAPAccessoryServerIncrementCN(&s_kvs) != kHAPError_None) {
      LOG(LL_ERROR, ("Failed to increment configuration number"));
    }
  }
  uint16_t cn;
  if (HAPAccessoryServerGetCN(&s_kvs, &cn) == kHAPError_None) {
    LOG(LL_INFO,
        ("=== Accessory provisioned, starting HAP server (CN %d)", cn));
  }

  if (s_accessory.services == nullptr) {
    s_accessory.services = CreateHAPServices();
  }

  HAPAccessoryServerStart(&s_server, &s_accessory);
  return true;
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
#if MGOS_HAVE_WIFI
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
#if MGOS_HAVE_WIFI
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
          static_cast<int>(HAPSwitch::InitialState::kLast));
    }
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
    if (mgos_sys_config_get_sw2_persist_state()) {
      mgos_sys_config_set_sw2_initial_state(
          static_cast<int>(HAPSwitch::InitialState::kLast));
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
  s_recreate_services = true;
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

  // Initialize inputs, outputs and other peripherals.
  //
  // Note for Shelly2.5: SW2 output (GPIO15) must be initialized before
  // SW1 input (GPIO13), doing it in reverse turns on SW2.
#ifdef MGOS_CONFIG_HAVE_SW2
  {
    const auto *sw2_cfg = mgos_sys_config_get_sw2();
    std::unique_ptr<InputPin> in2(
        new InputPin(2, sw2_cfg->in_gpio, 0, MGOS_GPIO_PULL_NONE, true));
    std::unique_ptr<OutputPin> out2(
        new OutputPin(2, sw2_cfg->out_gpio, sw2_cfg->out_on_value));
    in2->AddHandler(std::bind(&HandleInputResetSequence, in2.get(), _1, _2));
    s_outputs.push_back(std::move(out2));
    s_inputs.push_back(std::move(in2));
  }
#endif
#ifdef MGOS_CONFIG_HAVE_SW1
  {
    const auto *sw1_cfg = mgos_sys_config_get_sw1();
    std::unique_ptr<InputPin> in1(
        new InputPin(1, sw1_cfg->in_gpio, 0, MGOS_GPIO_PULL_NONE, true));
    std::unique_ptr<OutputPin> out1(
        new OutputPin(1, sw1_cfg->out_gpio, sw1_cfg->out_on_value));
    in1->AddHandler(std::bind(&HandleInputResetSequence, in1.get(), _1, _2));
    s_outputs.push_back(std::move(out1));
    s_inputs.push_back(std::move(in1));
  }
#endif
  auto pmss = PowerMeterInit();
  if (pmss.ok()) {
    s_pms = pmss.MoveValueOrDie();
  } else {
    LOG(LL_INFO,
        ("Power meter init failed: %s", pmss.status().error_message().c_str()));
  }

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

  s_accessory.name = mgos_sys_config_get_device_id();
  s_accessory.firmwareVersion = mgos_sys_ro_vars_get_fw_version();
  s_accessory.serialNumber = mgos_sys_config_get_device_sn();
  if (s_accessory.serialNumber == nullptr) {
    static char sn[13] = "????????????";
    mgos_expand_mac_address_placeholders(sn);
    s_accessory.serialNumber = sn;
  }

  // Initialize accessory server.
  HAPAccessoryServerCreate(&s_server, &s_server_options, &s_platform,
                           &s_callbacks, nullptr /* context */);

  StartHAPServer(false /* quiet */);

  // House-keeping timer.
  mgos_set_timer(1000, MGOS_TIMER_REPEAT, StatusTimerCB, nullptr);

  mgos_hap_add_rpc_service(&s_server, &s_accessory, &s_kvs);

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
