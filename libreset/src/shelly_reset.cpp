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

#include "shelly_reset.hpp"

#include "mgos.hpp"
#include "mgos_vfs.h"

#if CS_PLATFORM == CS_P_ESP8266
extern "C" {
#include "user_interface.h"
}
extern "C" uint32_t rtc_get_reset_reason(void);
// This location is in RTC memory and was used for MEMP_NUM_TCP_PCB in original
// LWIP It used RTC to communicate with espconn (ugh). This is not used anymore,
// so we can repurpose this location for failsafe flag.
#define RTC_SCRATCH_ADDR 0x600011fc
#elif CS_PLATFORM == CS_P_ESP32
#include "esp32/rom/rtc.h"
#define RTC_SCRATCH_ADDR 0x50001ffc
#endif

#define FF_MODE_MAGIC 0x18365472

#include "shelly_main.hpp"

namespace shelly {

static bool s_failsafe_mode = false;

// Executed very early, pretty much nothing is available here.
extern "C" void mgos_app_preinit(void) {
#if RST_GPIO_INIT >= 0
  mgos_gpio_setup_output(RST_GPIO_INIT, 0);
#endif
#if LED_GPIO >= 0
  mgos_gpio_setup_output(LED_GPIO, !LED_ON);
#endif
#if BTN_GPIO >= 0
  mgos_gpio_setup_input(BTN_GPIO,
                        (BTN_DOWN ? MGOS_GPIO_PULL_DOWN : MGOS_GPIO_PULL_UP));
#if CS_PLATFORM == CS_P_ESP8266
  // system_get_rst_info() is not available yet so we're on our own.
  uint32_t rir = READ_PERI_REG(RTC_STORE0);  // rst_info.reason
  // If this is not a power up / CH_PD reset, skip.
  if (rir == REASON_SOFT_RESTART) {
    s_failsafe_mode = (READ_PERI_REG(RTC_SCRATCH_ADDR) == FF_MODE_MAGIC);
    WRITE_PERI_REG(RTC_SCRATCH_ADDR, 0);
    return;
  }
#elif CS_PLATFORM == CS_P_ESP32
  if (IsSoftReboot()) {
    s_failsafe_mode = (READ_PERI_REG(RTC_SCRATCH_ADDR) == FF_MODE_MAGIC);
    WRITE_PERI_REG(RTC_SCRATCH_ADDR, 0);
    return;
  }
#endif
  // Give the user 3 seconds to press the button and hold it for 2 seconds.
  int num_down = 0;
  for (int i = 0; (i < 30 || num_down > 0) && num_down < 20; i++) {
    mgos_msleep(100);
    bool down = (mgos_gpio_read(BTN_GPIO) == BTN_DOWN);
    if (down) {
      mgos_cd_putc('!');
      num_down++;
    } else {
      mgos_cd_putc('.');
      num_down = 0;
    }
#if LED_GPIO >= 0
    if (down) {
      mgos_gpio_write(LED_GPIO, !LED_ON);
    } else {
      mgos_gpio_toggle(LED_GPIO);
    }
#endif
  }
  mgos_cd_putc('\n');
#if LED_GPIO >= 0
  mgos_gpio_write(LED_GPIO, !LED_ON);
#endif
  if (num_down >= 20) {
    s_failsafe_mode = true;
  }
#endif
}

bool IsFailsafeMode() {
  return s_failsafe_mode;
}

bool WipeDevice() {
  LOG(LL_INFO, ("== Wiping configuration"));
  static const char *s_wipe_files[] = {
      "conf2.json", "conf9.json", KVS_FILE_NAME, ACL_FILE_NAME, AUTH_FILE_NAME,
  };
  bool wiped = false;
  for (const char *wipe_fn : s_wipe_files) {
    if (remove(wipe_fn) == 0) wiped = true;
  }
#if defined(MGOS_HAVE_VFS_FS_SPIFFS) || defined(MGOS_HAVE_VFS_FS_LFS)
  if (wiped) {
    mgos_vfs_gc("/");
  }
#endif
  return wiped;
}

void SanitizeSysConfig() {
#ifdef MGOS_CONFIG_HAVE_WIFI
  struct mgos_config_wifi wifi_cfg = {};
  mgos_config_wifi_copy(mgos_sys_config_get_wifi(), &wifi_cfg);
  // Load config up to level 8, just before the user level.
  mgos_sys_config_load_level(&mgos_sys_config, MGOS_CONFIG_LEVEL_VENDOR_8);
  // Copy WiFi settings back.
  mgos_config_wifi_copy(&wifi_cfg, &mgos_sys_config.wifi);
  mgos_config_wifi_free(&wifi_cfg);
  // Save the config. Only WiFi settings will be saved to conf9.json.
  mgos_sys_config_save(&mgos_sys_config, false, nullptr);
#endif  // MGOS_CONFIG_HAVE_WIFI
}

void WipeDeviceRevertToStock() {
  // Files that we brought and want to remove so as not to pollute stock.
  static const char *s_wipe_files_stock[] = {
      "favicon.ico.gz",
  };
  for (const char *wipe_fn : s_wipe_files_stock) {
    remove(wipe_fn);
  }
  WipeDevice();
  SanitizeSysConfig();
}

bool IsSoftReboot() {
#if CS_PLATFORM == CS_P_ESP8266
  const struct rst_info *ri = system_get_rst_info();
  return (ri->reason == REASON_SOFT_RESTART);
#elif CS_PLATFORM == CS_P_ESP32
  RESET_REASON rr = rtc_get_reset_reason(0 /* core */);
  return (rr == SW_RESET || rr == SW_CPU_RESET);
#else
  return false;
#endif
}

extern "C" bool mgos_libreset_init(void) {
  if (!IsFailsafeMode()) return true;
  if (WipeDevice()) {
    LOG(LL_INFO, ("== Wiped config, rebooting"));
#ifdef RTC_SCRATCH_ADDR
    WRITE_PERI_REG(RTC_SCRATCH_ADDR, FF_MODE_MAGIC);
#endif
    mgos_system_restart_after(100);  // Not needed, but just in case.
    return false;                    // Will reboot the device.
  }
  return true;
}

}  // namespace shelly
