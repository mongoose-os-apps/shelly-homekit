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

namespace shelly {

StatusLED::StatusLED(int id, int pin, int num_pixel,
                     enum mgos_neopixel_order pixel_type, Output *chained_led,
                     const struct mgos_config_led *cfg)
    : Output(id),
      pin_(pin),
      num_pixel_(num_pixel),
      chained_led_(chained_led),
      cfg_(cfg) {
  pixel_ = mgos_neopixel_create(pin_, num_pixel_, pixel_type);
  value_ = false;
}

StatusLED::~StatusLED() {
  mgos_neopixel_free(pixel_);
}

bool StatusLED::GetState() {
  return value_;
}

int StatusLED::pin() const {
  return pin_;
}

struct rgb {
  int r;
  int g;
  int b;
};

Status StatusLED::SetState(bool on, const char *source) {
  if (chained_led_ != nullptr) {
    chained_led_->SetState(on, source);
  }
  value_ = on;
  // get color from config
  struct rgb colorOn = {(cfg_->color_on >> 16) & 0xFF,
                        (cfg_->color_on >> 8) & 0xFF,
                        (cfg_->color_on >> 0) & 0xFF};
  struct rgb colorOff = {(cfg_->color_off >> 16) & 0xFF,
                         (cfg_->color_off >> 8) & 0xFF,
                         (cfg_->color_off >> 0) & 0xFF};

  struct rgb color = on ? colorOn : colorOff;

  for (int i = 0; i < num_pixel_; i++) {
    mgos_neopixel_set(pixel_, i, color.r, color.g, color.b);
  }
  mgos_neopixel_show(pixel_);
  return Status::OK();
}

}  // namespace shelly

#endif
