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

#include "shelly_noisy_input_pin.hpp"

#include "mgos.h"

#if CS_PLATFORM == CS_P_ESP8266
#include <user_interface.h>
typedef uint16_t sample_t;
inline static sample_t ReadGPIOReg() {
  uint32_t gpio_vals = GPIO_REG_READ(GPIO_IN_ADDRESS);
  // gpio_vals |= (READ_PERI_REG(RTC_GPIO_IN_DATA) & 1) << 16;  // GPIO16
  return gpio_vals;
}
#elif CS_PLATFORM == CS_P_ESP32
#include "soc/gpio_reg.h"
// Note: supports only GPIO 0-31, for obvious reasons.
typedef uint32_t sample_t;
inline static sample_t ReadGPIOReg() {
  return READ_PERI_REG(GPIO_IN_REG);
}
#endif

namespace shelly {

#define NUM_SAMPLES 10
#define SAMPLE_INTERVAL_MICROS 5000

static sample_t s_gpio_vals[NUM_SAMPLES] = {0};
static sample_t s_gpio_mask = 0, s_gpio_last = 0;
static uint8_t s_cnt = 0;
static volatile uint8_t s_meas_cnt = 0;
static mgos_timer_id s_timer_id = MGOS_INVALID_TIMER_ID;

static std::vector<NoisyInputPin *> s_noisy_inputs;

static void GPIOChangeCB(void *arg) {
  // Check all inputs. If nothing changed it will be a no-op.
  for (NoisyInputPin *in : s_noisy_inputs) {
    in->Check();
  }
  (void) arg;
}

/* NB: Executed in ISR context */
static IRAM void GPIOHWTimerCB(void *arg) {
  sample_t gpio_vals = ReadGPIOReg();
  gpio_vals &= s_gpio_mask;
  s_gpio_vals[s_cnt++] = gpio_vals;
  if (s_cnt == NUM_SAMPLES) s_cnt = 0;
  int i = 0;
  // Wait for consistent samples.
  for (i = 0; i < NUM_SAMPLES; i++) {
    if (s_gpio_vals[i] != gpio_vals) return;
  }
  s_meas_cnt++;
  // Has anything changed?
  if (s_gpio_last == gpio_vals) return;
  s_gpio_last = gpio_vals;
  mgos_invoke_cb(GPIOChangeCB, nullptr, true /* from_isr */);
  (void) arg;
}

NoisyInputPin::NoisyInputPin(int id, int pin, int on_value,
                             enum mgos_gpio_pull_type pull, bool enable_reset)
    : InputPin(id, pin, on_value, pull, enable_reset) {
}

NoisyInputPin::NoisyInputPin(int id, const InputPin::Config &cfg)
    : InputPin(id, cfg) {
}

NoisyInputPin::~NoisyInputPin() {
  s_gpio_mask &= ~(1 << cfg_.pin);
  for (auto it = s_noisy_inputs.begin(); it != s_noisy_inputs.end(); it++) {
    if (*it == this) {
      s_noisy_inputs.erase(it);
      break;
    }
  }
  s_noisy_inputs.shrink_to_fit();
}

void NoisyInputPin::Init() {
  s_noisy_inputs.push_back(this);
  s_noisy_inputs.shrink_to_fit();
  mgos_gpio_setup_input(cfg_.pin, cfg_.pull);
  s_gpio_mask |= (1 << cfg_.pin);
  if (s_timer_id == MGOS_INVALID_TIMER_ID) {
    LOG(LL_INFO, ("Starting sampling timer"));
    s_timer_id = mgos_set_hw_timer(SAMPLE_INTERVAL_MICROS, MGOS_TIMER_REPEAT,
                                   GPIOHWTimerCB, nullptr);
  }
  uint32_t mc = s_meas_cnt;
  while (s_meas_cnt == mc) {
    // Spin.
  }
  bool state = GetState();
  LOG(LL_INFO, ("%s %d: pin %d, on_value %d, state %s mc %d %#x",
                "NoisyInputPin", id(), cfg_.pin, cfg_.on_value, OnOff(state),
                (int) s_meas_cnt, (unsigned) s_gpio_last));
}

void NoisyInputPin::Check() {
  HandleGPIOInt();
}

bool NoisyInputPin::ReadPin() {
  return (s_gpio_last & (1 << cfg_.pin)) != 0;
}

}  // namespace shelly
