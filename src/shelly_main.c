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
#include "mgos_dns_sd.h"
#include "mgos_hap.h"
#ifdef MGOS_HAVE_OTA_COMMON
#include "mgos_ota.h"
#endif
#include "mgos_rpc.h"

#include "HAPPlatform+Init.h"
#include "HAPPlatformAccessorySetup+Init.h"
#include "HAPPlatformKeyValueStore+Init.h"
#include "HAPPlatformServiceDiscovery+Init.h"
#include "HAPPlatformTCPStreamManager+Init.h"

#include "shelly_sw_service.h"

#define KVS_FILE_NAME "kvs.json"
#define NUM_SESSIONS 3
#define IO_BUF_SIZE 2048

#ifndef LED_ON
#define LED_ON 0
#endif
#ifndef BTN_DOWN
#define BTN_DOWN 0
#endif

static HAPIPSession sessions[NUM_SESSIONS];
static uint8_t in_bufs[NUM_SESSIONS][IO_BUF_SIZE];
static uint8_t out_bufs[NUM_SESSIONS][IO_BUF_SIZE];
static uint8_t scratch_buf[IO_BUF_SIZE];
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
    .ip =
        {
            .transport = &kHAPAccessoryServerTransport_IP,
            .accessoryServerStorage = &s_ip_storage,
        },
};
static HAPAccessoryServerCallbacks s_callbacks;
static HAPPlatformTCPStreamManager s_tcp_stream_manager;
static HAPPlatformServiceDiscovery s_service_discovery;
static HAPAccessoryServerRef s_server;

static HAPPlatform s_platform = {
    .keyValueStore = &s_kvs,
    .accessorySetup = &s_accessory_setup,
    .ip =
        {
            .tcpStreamManager = &s_tcp_stream_manager,
            .serviceDiscovery = &s_service_discovery,
        },
};

HAPError shelly_identify_cb(HAPAccessoryServerRef *server,
                            const HAPAccessoryIdentifyRequest *request,
                            void *context);

static HAPAccessory s_accessory = {
    .aid = 1,
    .category = kHAPAccessoryCategory_Switches,
    .name = NULL,  // Set from config,
    .manufacturer = CS_STRINGIFY_MACRO(PRODUCT_VENDOR),
    .model = CS_STRINGIFY_MACRO(PRODUCT_MODEL),
    .serialNumber = NULL,     // Set from config.
    .firmwareVersion = NULL,  // Set from build_id.
    .hardwareVersion = CS_STRINGIFY_MACRO(PRODUCT_HW_REV),
    .services = NULL,  // Set later
    .callbacks = {.identify = shelly_identify_cb},
};

static int16_t s_btn_pressed_count = 0;
static int16_t s_identify_count = 0;

static void check_led(int pin, bool led_on);

HAPError shelly_identify_cb(HAPAccessoryServerRef *server,
                            const HAPAccessoryIdentifyRequest *request,
                            void *context) {
  LOG(LL_INFO, ("=== IDENTIFY ==="));
  s_identify_count = 3;
  check_led(LED_GPIO, LED_ON);
  (void) server;
  (void) request;
  (void) context;
  return kHAPError_None;
}

static void shelly_hap_server_state_update_cb(HAPAccessoryServerRef *server,
                                              void *context) {
  LOG(LL_INFO, ("HAP server state: %d", HAPAccessoryServerGetState(server)));
  (void) context;
}

static bool shelly_start_hap_server(bool quiet) {
  if (HAPAccessoryServerGetState(&s_server) != kHAPAccessoryServerState_Idle) {
    return true;
  }
  if (mgos_hap_config_valid()) {
    LOG(LL_INFO, ("=== Accessory provisioned, starting HAP server"));
    HAPAccessoryServerStart(&s_server, &s_accessory);
    return true;
  } else if (!quiet) {
    LOG(LL_INFO, ("=== Accessory not provisioned"));
  }
  return false;
}

static void check_btn(int pin, bool btn_down) {
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
    LOG(LL_INFO, ("Re-enabling AP"));
#ifdef MGOS_SYS_CONFIG_HAVE_WIFI
    mgos_sys_config_set_wifi_sta_enable(false);
    mgos_sys_config_set_wifi_ap_enable(true);
    mgos_sys_config_save(&mgos_sys_config, false, NULL);
    mgos_wifi_setup((struct mgos_config_wifi *) mgos_sys_config_get_wifi());
#endif
  }
}

static void check_led(int pin, bool led_act) {
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
  if (HAPAccessoryServerGetState(&s_server) == kHAPAccessoryServerState_Idle) {
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

static void shelly_status_timer_cb(void *arg) {
  static bool s_tick_tock = false;
  LOG(LL_INFO,
      ("%s uptime: %.2lf, RAM: %lu, %lu free", (s_tick_tock ? "Tick" : "Tock"),
       mgos_uptime(), (unsigned long) mgos_get_heap_size(),
       (unsigned long) mgos_get_free_heap_size()));
  s_tick_tock = !s_tick_tock;
  /* If provisioning information has been provided, start the server. */
  shelly_start_hap_server(true /* quiet */);
  check_btn(BTN_GPIO, BTN_DOWN);
  check_led(LED_GPIO, LED_ON);
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

static void shelly_get_info_handler(struct mg_rpc_request_info *ri,
                                    void *cb_arg, struct mg_rpc_frame_info *fi,
                                    struct mg_str args) {
  const char *ssid = mgos_sys_config_get_wifi_sta_ssid();
  const char *pass = mgos_sys_config_get_wifi_sta_pass();
  bool hap_provisioned = !mgos_conf_str_empty(mgos_sys_config_get_hap_salt());
  bool hap_paired = HAPAccessoryServerIsPaired(&s_server);
#ifdef MGOS_CONFIG_HAVE_SW1
  struct shelly_sw_info sw1;
  shelly_sw_get_info(mgos_sys_config_get_sw1_id(), &sw1);
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
  struct shelly_sw_info sw2;
  shelly_sw_get_info(mgos_sys_config_get_sw2_id(), &sw2);
#endif
  mg_rpc_send_responsef(
      ri,
      "{id: %Q, app: %Q, host: %Q, version: %Q, fw_build: %Q, "
#ifdef MGOS_CONFIG_HAVE_SW1
      "sw1: {id: %d, name: %Q, in_mode: %d, persist: %B, state: %B},"
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
      "sw2: {id: %d, name: %Q, in_mode: %d, persist: %B, state: %B},"
#endif
      "wifi_en: %B, wifi_ssid: %Q, wifi_pass: %Q, "
      "hap_provisioned: %B, hap_paired: %B}",
      mgos_sys_config_get_device_id(), MGOS_APP, mgos_dns_sd_get_host_name(),
      mgos_sys_ro_vars_get_fw_version(), mgos_sys_ro_vars_get_fw_id(),
#ifdef MGOS_CONFIG_HAVE_SW1
      mgos_sys_config_get_sw1_id(), mgos_sys_config_get_sw1_name(),
      mgos_sys_config_get_sw1_in_mode(),
      mgos_sys_config_get_sw1_persist_state(), sw1.state,
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
      mgos_sys_config_get_sw2_id(), mgos_sys_config_get_sw2_name(),
      mgos_sys_config_get_sw2_in_mode(),
      mgos_sys_config_get_sw2_persist_state(), sw2.state,
#endif
      mgos_sys_config_get_wifi_sta_enable(), (ssid ? ssid : ""),
      (pass ? pass : ""), hap_provisioned, hap_paired);
  (void) cb_arg;
  (void) fi;
  (void) args;
}

static void shelly_set_switch_handler(struct mg_rpc_request_info *ri,
                                      void *cb_arg,
                                      struct mg_rpc_frame_info *fi,
                                      struct mg_str args) {
  int id = -1;
  bool state = false;

  json_scanf(args.p, args.len, ri->args_fmt, &id, &state);

  if (shelly_sw_set_state(id, state, "web")) {
    mg_rpc_send_responsef(ri, NULL);
  } else {
    mg_rpc_send_errorf(ri, 400, "bad args");
  }

  (void) cb_arg;
  (void) fi;
}

enum mgos_app_init_result mgos_app_init(void) {
#ifdef MGOS_HAVE_OTA_COMMON
  if (mgos_ota_is_first_boot()) {
    LOG(LL_INFO, ("Performing cleanup"));
    // In case we're uograding from stock fw, remove its files
    // with the exception of hwinfo_struct.json.
    remove("cert.pem");
    remove("passwd");
    remove("relaydata");
  }
#endif

  // Key-value store.
  HAPPlatformKeyValueStoreCreate(
      &s_kvs,
      &(const HAPPlatformKeyValueStoreOptions){.fileName = KVS_FILE_NAME});

  // Accessory setup.
  HAPPlatformAccessorySetupCreate(&s_accessory_setup,
                                  &(const HAPPlatformAccessorySetupOptions){});

  // TCP Stream Manager.
  HAPPlatformTCPStreamManagerCreate(
      &s_tcp_stream_manager, &(const HAPPlatformTCPStreamManagerOptions){
                                 .port = kHAPNetworkPort_Any,
                                 .maxConcurrentTCPStreams = NUM_SESSIONS});

  // Service discovery.
  HAPPlatformServiceDiscoveryCreate(
      &s_service_discovery, &(const HAPPlatformServiceDiscoveryOptions){});

  for (size_t i = 0; i < ARRAY_SIZE(sessions); i++) {
    sessions[i].inboundBuffer.bytes = in_bufs[i];
    sessions[i].inboundBuffer.numBytes = sizeof(in_bufs[i]);
    sessions[i].outboundBuffer.bytes = out_bufs[i];
    sessions[i].outboundBuffer.numBytes = sizeof(out_bufs[i]);
  }

  s_server_options.maxPairings = kHAPPairingStorage_MinElements;

  s_callbacks.handleUpdatedState = shelly_hap_server_state_update_cb;

  s_accessory.name = mgos_sys_config_get_device_id();
  s_accessory.firmwareVersion = mgos_sys_ro_vars_get_fw_version();
  s_accessory.serialNumber = mgos_sys_config_get_device_sn();

  const HAPService **services = calloc(3 + NUM_SWITCHES + 1, sizeof(*services));
  services[0] = &mgos_hap_accessory_information_service;
  services[1] = &mgos_hap_protocol_information_service;
  services[2] = &mgos_hap_pairing_service;
  int i = 3;
  if (mgos_sys_config_get_sw1_enable()) {
#ifdef MGOS_CONFIG_HAVE_SW1
    services[i++] = shelly_sw_service_create(mgos_sys_config_get_sw1());
#endif
#ifdef MGOS_CONFIG_HAVE_SW2
    services[i++] = shelly_sw_service_create(mgos_sys_config_get_sw2());
#endif
  }
  s_accessory.services = services;
  LOG(LL_INFO, ("Exported %d of %d switches", i - 3, NUM_SWITCHES));

  // Initialize accessory server.
  HAPAccessoryServerCreate(&s_server, &s_server_options, &s_platform,
                           &s_callbacks, NULL /* context */);

  shelly_start_hap_server(false /* quiet */);

  // Timer for periodic status.
  mgos_set_timer(1000, MGOS_TIMER_REPEAT, shelly_status_timer_cb, NULL);

  mgos_hap_add_rpc_service(&s_server, &s_accessory, &s_kvs);

  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.GetInfo", "",
                     shelly_get_info_handler, NULL);

  mg_rpc_add_handler(mgos_rpc_get_global(), "Shelly.SetSwitch",
                     "{id: %d, state: %B}", shelly_set_switch_handler, NULL);

  if (BTN_GPIO > 0) {
    mgos_gpio_setup_input(BTN_GPIO, MGOS_GPIO_PULL_UP);
  }

  return MGOS_APP_INIT_SUCCESS;
}
