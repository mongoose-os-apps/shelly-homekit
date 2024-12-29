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

#pragma once

#include "shelly_output.hpp"

#include "mgos_config.h"
#include "mgos_neopixel.h"

namespace shelly {

class StatusLED : public Output {
 public:
  StatusLED(int id, int pin, int num_pixel, enum mgos_neopixel_order pixel_type,
            Output *chained_led, const struct mgos_config_led *cfg);
  virtual ~StatusLED();

  // Output interface impl.
  bool GetState() override;
  Status SetState(bool on, const char *source) override;
  Status SetStatePWM(float duty, const char *source) override {
    return Status::UNIMPLEMENTED();
  };
  Status Pulse(bool on, int duration_ms, const char *source) override {
    return Status::UNIMPLEMENTED();
  };
  void SetInvert(bool out_invert) override{};
  int pin() const;

 protected:
 private:
  const int pin_;
  const int num_pixel_;

  bool value_;

  struct mgos_neopixel *pixel_;

  Output *chained_led_;

  const struct mgos_config_led *cfg_;

  StatusLED(const StatusLED &other) = delete;
};

}  // namespace shelly
