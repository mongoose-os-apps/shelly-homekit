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

#pragma once

#include <memory>
#include <vector>

#include "mgos_sys_config.h"
#include "mgos_timers.h"

#include "shelly_common.hpp"
#include "shelly_component.hpp"
#include "shelly_hap_chars.hpp"
#include "shelly_hap_service.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"
#include "shelly_pm.hpp"

namespace shelly {
namespace hap {

// Common base for Switch, Outlet and Lock services.
class WindowCovering : public Component, public Service {
 public:
  WindowCovering(int id, Input *in0, Input *in1, Output *out0, Output *out1,
                 PowerMeter *pm0, PowerMeter *pm1, struct mgos_config_wc *cfg);
  virtual ~WindowCovering();

  Status Init() override;

  // Component interface impl.
  Type type() const override {
    return Type::kWindowCovering;
  }
  StatusOr<std::string> GetInfo() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;

 private:
  void HandleInputEvent(int index, Input::Event ev, bool state);

  HAPError HandleTargetPositionWrite(
      HAPAccessoryServerRef *server,
      const HAPUInt8CharacteristicWriteRequest *request, uint8_t value);

  std::vector<Input *> inputs_;
  std::vector<Output *> outputs_;
  std::vector<PowerMeter *> pms_;
  struct mgos_config_wc *cfg_;

  uint8_t cur_pos_ = 0;
  uint8_t tgt_pos_ = 0;

  std::vector<Input::HandlerID> input_handlers_;
};

}  // namespace hap
}  // namespace shelly
