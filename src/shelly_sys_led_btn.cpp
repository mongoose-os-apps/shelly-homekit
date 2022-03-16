
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

#include "shelly_sys_led_btn.hpp"

#include "mgos.hpp"

#include "mgos_hap.hpp"
#include "mgos_ota.h"

#include "shelly_component.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_noisy_input_pin.hpp"
#include "shelly_switch.hpp"
#include "shelly_wifi_config.hpp"

namespace shelly {

static Input *s_btn = nullptr;
static uint8_t s_identify_count = 0;
static int8_t s_led_gpio = -1;
static int8_t s_led_enable = false;
static bool s_active_high = false;

static HAPError SysLEDIdentifyCB(
    const HAPAccessoryIdentifyRequest *request UNUSED_ARG) {
  LOG(LL_INFO, ("=== IDENTIFY ==="));
  s_identify_count = 3;
  CheckSysLED();
  return kHAPError_None;
}

void InitSysLED(int gpio, bool active_high) {
  s_led_gpio = gpio;
  s_active_high = active_high;
  s_led_enable = true;
  SetIdentifyCB(SysLEDIdentifyCB);
}

void CheckSysLED() {
  if (s_led_gpio < 0 || !s_led_enable) return;
  int pin = s_led_gpio;
  int on_ms = 0, off_ms = 0;
  static int s_on_ms = 0, s_off_ms = 0;
  const WifiInfo &wi = GetWifiInfo();
  const WifiConfig &wc = GetWifiConfig();
  // Identify sequence requested by controller.
  if (s_identify_count > 0) {
    LOG(LL_DEBUG, ("LED: identify (%d)", s_identify_count));
    on_ms = 100;
    off_ms = 100;
    s_identify_count--;
    goto out;
  }
  // If user is currently holding the button, acknowledge it.
  if (s_btn != nullptr && s_btn->GetState()) {
    LOG(LL_DEBUG, ("LED: btn"));
    on_ms = 1;
    off_ms = 0;
    goto out;
  }
  // Are we connecting to wifi right now?
  if (wi.sta_connecting) {
    LOG(LL_DEBUG, ("LED: WiFi"));
    on_ms = 200;
    off_ms = 200;
    goto out;
  }
  if (mgos_ota_is_in_progress()) {
    LOG(LL_DEBUG, ("LED: OTA"));
    on_ms = 250;
    off_ms = 250;
    goto out;
  }
  // Indicate WiFi provisioning status.
  if (wi.ap_running && !(wc.sta.enable || wc.sta1.enable)) {
    LOG(LL_DEBUG, ("LED: WiFi provisioning"));
    off_ms = 25;
    on_ms = 875;
    goto out;
  }
  // HAP server status (if WiFi is provisioned).
  if (!IsServiceRunning()) {
    off_ms = 875;
    on_ms = 25;
    LOG(LL_DEBUG, ("LED: HAP provisioning"));
  } else if (!IsPaired()) {
    LOG(LL_DEBUG, ("LED: Pairing"));
    off_ms = 500;
    on_ms = 500;
  }
out:
  if (on_ms > 0) {
    if (on_ms > 1) {
      mgos_gpio_set_mode(pin, MGOS_GPIO_MODE_OUTPUT);
      if (on_ms != s_on_ms || off_ms != s_off_ms) {
        if (s_active_high) {
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
      mgos_gpio_setup_output(pin, s_active_high);
    }
  } else {
    mgos_gpio_set_mode(pin, MGOS_GPIO_MODE_INPUT);
  }
}

static void ButtonHandler(Input::Event ev, bool cur_state) {
  switch (ev) {
    case Input::Event::kChange: {
      CheckSysLED();
      break;
    }
    // Single press will toggle the switch, or cycle if there are two.
    case Input::Event::kSingle: {
      uint32_t n = 0, i = 0, state = 0;
      for (auto &c : g_comps) {
        if (c->type() != Component::Type::kSwitch) continue;
        const ShellySwitch *sw = static_cast<ShellySwitch *>(c.get());
        if (sw->GetOutputState()) state |= (1 << n);
        n++;
      }
      if (n == 0) break;
      state++;
      for (auto &c : g_comps) {
        if (c->type() != Component::Type::kSwitch) continue;
        ShellySwitch *sw = static_cast<ShellySwitch *>(c.get());
        bool new_state = (state & (1 << i));
        sw->SetOutputState(new_state, "btn");
        i++;
      }
      break;
    }
    case Input::Event::kLong: {
      HandleInputResetSequence(s_btn, s_led_gpio, Input::Event::kReset,
                               cur_state);
      break;
    }
    default:
      break;
  }
}

void InitSysBtn(int pin, bool on_value) {
  if (pin < 0) return;
  const auto cfg = InputPin::Config{
      .pin = pin,
      .on_value = on_value,
      .pull = (on_value ? MGOS_GPIO_PULL_DOWN : MGOS_GPIO_PULL_UP),
      .enable_reset = false,
      .short_press_duration_ms = InputPin::kDefaultShortPressDurationMs,
      .long_press_duration_ms = 10000,
  };
#if BTN_NOISY
  s_btn = new NoisyInputPin(0, cfg);
#else
  s_btn = new InputPin(0, cfg);
#endif
  s_btn->Init();
  s_btn->AddHandler(ButtonHandler);
}

}  // namespace shelly
