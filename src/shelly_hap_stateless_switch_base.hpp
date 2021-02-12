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

#include <memory>
#include <vector>

#include "mgos_hap.hpp"
#include "mgos_sys_config.h"
#include "mgos_timers.h"

#include "shelly_common.hpp"
#include "shelly_component.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"
#include "shelly_pm.hpp"

namespace shelly {
namespace hap {

// Common base for Switch, Outlet and Lock services.
class StatelessSwitchBase : public Component, public mgos::hap::Service {
 public:
  enum class InMode {
    kMomentary = 0,
    kToggleShort = 1,
    kToggleShortLong = 2,
  };

  StatelessSwitchBase(int id, Input *in, struct mgos_config_in_ssw *cfg,
                      uint16_t iid_base, const HAPUUID *type,
                      const char *debug_description);
  virtual ~StatelessSwitchBase();

  // Component interface impl.
  Status Init() override;
  Type type() const override;
  std::string name() const override;
  StatusOr<std::string> GetInfo() const override;
  StatusOr<std::string> GetInfoJSON() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;
  Status SetState(const std::string &state_json) override;

 private:
  void InputEventHandler(Input::Event ev, bool state);

  void RaiseEvent(uint8_t ev);

  Input *const in_;
  struct mgos_config_in_ssw *cfg_;

  Input::HandlerID handler_id_ = Input::kInvalidHandlerID;

  uint8_t last_ev_ = 0;
  double last_ev_ts_ = 0;

  StatelessSwitchBase(const StatelessSwitchBase &other) = delete;
};

}  // namespace hap
}  // namespace shelly
