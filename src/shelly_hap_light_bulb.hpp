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

#include "mgos_hap_chars.hpp"
#include "mgos_hap_service.hpp"
#include "mgos_sys_config.h"
#include "mgos_timers.hpp"

#include "shelly_common.hpp"
#include "shelly_component.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"

namespace shelly {
namespace hap {

class LightBulb : public Component, public mgos::hap::Service {
 public:
  LightBulb(int id, Input *in, Output *out_r, Output *out_g, Output *out_b,
            Output *out_w, struct mgos_config_lb *cfg);
  virtual ~LightBulb();

  struct RGBW {
    float r;
    float g;
    float b;
    float w;
  };

  // Component interface impl.
  Type type() const override;
  std::string name() const override;
  Status Init() override;

  StatusOr<std::string> GetInfo() const override;
  StatusOr<std::string> GetInfoJSON() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;
  Status SetState(const std::string &state_json) override;

 protected:
  void InputEventHandler(Input::Event ev, bool state);

  void AutoOffTimerCB();
  void TransitionTimerCB();

  void UpdateOnOff(bool on, const std::string &source, bool force = false);
  void SetHue(int hue, const std::string &source);
  void SetSaturation(int saturation, const std::string &source);
  void SetBrightness(int brightness, const std::string &source);

  bool IsOn() const;
  bool IsOff() const;
  bool IsAutoOffEnabled() const;

  void HSVtoRGBW(RGBW &rgbw) const;
  void StartTransition();
  void SaveState();
  void ResetAutoOff();
  void DisableAutoOff();

  Input *const in_;
  Output *const out_r_, *const out_g_, *const out_b_, *const out_w_;
  struct mgos_config_lb *cfg_;

  Input::HandlerID handler_id_ = Input::kInvalidHandlerID;
  mgos::hap::BoolCharacteristic *on_characteristic;
  mgos::hap::UInt8Characteristic *brightness_characteristic;
  mgos::hap::UInt32Characteristic *hue_characteristic;
  mgos::hap::UInt32Characteristic *saturation_characteristic;

  mgos::Timer auto_off_timer_;
  bool dirty_ = false;

  mgos::Timer transition_timer_;
  int64_t transition_start_ = 0;
  RGBW rgbw_start_{};
  RGBW rgbw_now_{};
  RGBW rgbw_end_{};
};

}  // namespace hap
}  // namespace shelly
