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
class StatelessSwitch : public Component, public Service {
 public:
  enum class InMode {
    kMomentary = 0,
    kToggleShort = 1,
    kToggleShortLong = 2,
  };

  StatelessSwitch(int id, Input *in, struct mgos_config_ssw *cfg,
                  const uint16_t label_service_iid = 0);
  virtual ~StatelessSwitch();

  Status Init() override;

  // Component interface impl.
  Type type() const override {
    return Type::kStatelessSwitch;
  }
  StatusOr<std::string> GetInfo() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;

 private:
  void InputEventHandler(Input::Event ev, bool state);

  void RaiseEvent(uint8_t ev);

  Input *const in_;
  struct mgos_config_ssw *cfg_;

  Input::HandlerID handler_id_ = Input::kInvalidHandlerID;

  uint8_t last_ev_ = 0;
  double last_ev_ts_ = 0;

  StatelessSwitch(const StatelessSwitch &other) = delete;
};

}  // namespace hap
}  // namespace shelly
