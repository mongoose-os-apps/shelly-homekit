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

#include "shelly_noisy_input_pin.hpp"

#include "mgos.h"

#include <user_interface.h>

namespace shelly {

#define NUM_SAMPLES 5
#define SAMPLE_INTERVAL_MICROS 5000
static uint32_t s_gpio_vals[NUM_SAMPLES] = {0};
static uint32_t s_gpio_mask = 0, s_gpio_last = 0;
static uint32_t s_cnt = 0, s_meas_cnt = 0;
mgos_timer_id s_timer_id = MGOS_INVALID_TIMER_ID;

std::vector<NoisyInputPin *> s_noisy_inputs;

static IRAM void GPIOChangeCB(void *arg) {
  // Check all inputs. If nothing changed it will be a no-op.
  for (NoisyInputPin *in : s_noisy_inputs) {
    in->Check();
  }
  (void) arg;
}

/* NB: Executed in ISR context */
static IRAM void GPIOHWTimerCB(void *arg) {
  uint32_t gpio_vals = GPIO_REG_READ(GPIO_IN_ADDRESS) & 0xffff;
  // gpio_vals |= (READ_PERI_REG(RTC_GPIO_IN_DATA) & 1) << 16;  // GPIO16
  gpio_vals &= s_gpio_mask;
  s_gpio_vals[s_cnt++] = gpio_vals;
  if (s_cnt == NUM_SAMPLES) s_cnt = 0;
  int i = 0;
  // Wait for consistent samples.
  for (i = 0; i < NUM_SAMPLES; i++) {
    if (s_gpio_vals[i] != gpio_vals) return;
  }
  // Has anything changed?
  if (s_meas_cnt != 0 && s_gpio_last == gpio_vals) return;
  s_gpio_last = gpio_vals;
  s_meas_cnt++;
  mgos_invoke_cb(GPIOChangeCB, nullptr, true /* from_isr */);
  (void) arg;
}

NoisyInputPin::NoisyInputPin(int id, int pin, int on_value,
                             enum mgos_gpio_pull_type pull, bool enable_reset)
    : InputPin(id, pin, on_value, pull, enable_reset) {
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
  s_gpio_mask |= (1 << cfg_.pin);
  if (s_timer_id == MGOS_INVALID_TIMER_ID) {
    mgos_set_hw_timer(SAMPLE_INTERVAL_MICROS, MGOS_TIMER_REPEAT, GPIOHWTimerCB,
                      nullptr);
  }
  GetState();
}

void NoisyInputPin::Check() {
  HandleGPIOInt();
}

bool NoisyInputPin::ReadPin() {
  return (s_gpio_last & (1 << cfg_.pin)) != 0;
}

}  // namespace shelly
