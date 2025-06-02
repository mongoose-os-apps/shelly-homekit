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

#include "mgos_config.h"

#ifdef MGOS_CONFIG_HAVE_LED

#pragma once

#include "shelly_component.hpp"
#include "shelly_output.hpp"

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
  Status SetStatePWM(float duty UNUSED_ARG,
                     const char *source UNUSED_ARG) override {
    return Status::UNIMPLEMENTED();
  };
  Status Pulse(bool on UNUSED_ARG, int duration_ms UNUSED_ARG,
               const char *source UNUSED_ARG) override {
    return Status::UNIMPLEMENTED();
  };
  void SetInvert(bool out_invert UNUSED_ARG) override{};
  int pin() const;

  struct mgos_config_led *GetConfig() const {
    return (struct mgos_config_led *) cfg_;
  };

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

class StatusLEDComponent : public Component {
 public:
  StatusLEDComponent(StatusLED *output);
  virtual ~StatusLEDComponent();

  // Component interface impl.
  Type type() const override {
    return Component::Type::kStatusLED;
  };
  std::string name() const override {
    return "Status LED";
  };
  Status Init() override;
  StatusOr<std::string> GetInfo() const override {
    return Status::OK();
  };
  StatusOr<std::string> GetInfoJSON() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;
  Status SetState(const std::string &state_json UNUSED_ARG) override {
    return Status::OK();
  };
  void Identify() override{

  };
  bool IsIdle() override {
    return true;  // Status LED is always idle.
  };

 private:
  struct mgos_config_led *cfg_;
};

}  // namespace shelly

#endif