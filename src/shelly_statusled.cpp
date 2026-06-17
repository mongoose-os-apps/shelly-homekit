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
#include "shelly_statusled.hpp"

#ifdef MGOS_CONFIG_HAVE_LED

#include "mgos_bitbang.h"
#include "mgos_gpio.h"
#include "mgos_system.h"

namespace shelly {

StatusLED::StatusLED(int id, int pin, int num_pixel,
                     enum mgos_neopixel_order pixel_type, Output *chained_led,
                     const struct mgos_config_led *cfg)
    : Output(id),
      pin_(pin),
      num_pixel_(num_pixel),
      chained_led_(chained_led),
      cfg_(cfg) {
  value_ = false;
}

StatusLED::~StatusLED() {
}

bool StatusLED::GetState() {
  return value_;
}

int StatusLED::pin() const {
  return pin_;
}

struct rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

Status StatusLED::SetState(bool on, const char *source) {
  if (chained_led_ != nullptr) {
    chained_led_->SetState(on, source);
  }
  value_ = on;
  // get color from config
  uint8_t mask = 0xFF;
  struct rgb colorOn = {(uint8_t) ((cfg_->color_on >> 16) & mask),
                        (uint8_t) ((cfg_->color_on >> 8) & mask),
                        (uint8_t) ((cfg_->color_on >> 0) & mask)};
  struct rgb colorOff = {(uint8_t) ((cfg_->color_off >> 16) & mask),
                         (uint8_t) ((cfg_->color_off >> 8) & mask),
                         (uint8_t) ((cfg_->color_off >> 0) & mask)};

  struct rgb color = on ? colorOn : colorOff;

  uint8_t data[num_pixel_ * 3];
  for (int i = 0; i < num_pixel_; i++) {
    data[i * 3 + 0] = color.g;
    data[i * 3 + 1] = color.r;
    data[i * 3 + 2] = color.b;
  }

  mgos_gpio_write(pin_, 0);
  mgos_usleep(300);
  // first pixel seems still get first wrong bit, unknown reason so far
  // original code is 3 8 8 3, but it seems that 4 10 10 4 works better for
  // WS2812B, other timings do not make a difference, also setting gpio to 1
  // before does not work
  mgos_bitbang_write_bits(pin_, MGOS_DELAY_100NSEC, 4, 10, 10, 4, data,
                          num_pixel_ * 3);
  mgos_gpio_write(pin_, 0);
  mgos_usleep(300);
  mgos_gpio_write(pin_, 1);
  return Status::OK();
}

}  // namespace shelly

#endif
