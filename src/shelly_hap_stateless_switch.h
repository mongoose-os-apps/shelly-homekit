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

#include "shelly_common.h"
#include "shelly_component.h"
#include "shelly_hap.h"
#include "shelly_hap_chars.h"
#include "shelly_input.h"
#include "shelly_output.h"
#include "shelly_pm.h"

namespace shelly {
namespace hap {

// Common base for Switch, Outlet and Lock services.
class StatelessSwitch : public Component, Service {
 public:
  StatelessSwitch(int id, Input *in, struct mgos_config_ssw *cfg,
                  HAPAccessoryServerRef *server, const HAPAccessory *accessory);
  virtual ~StatelessSwitch();

  Status Init() override;

  // Component interface impl.
  Type type() const override {
    return Type::kStatelessSwitch;
  }
  StatusOr<std::string> GetInfo() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;

  // HAP Service interface impl.
  const HAPService *GetHAPService() const override;

 private:
  void InputEventHandler(Input::Event ev, bool state);

  HAPError HandleEventRead(HAPAccessoryServerRef *server,
                           const HAPUInt8CharacteristicReadRequest *request,
                           uint8_t *value);

  HAPError HandleServiceLabelIndexRead(
      HAPAccessoryServerRef *server,
      const HAPUInt8CharacteristicReadRequest *request, uint8_t *value);

  Input *const in_;
  struct mgos_config_ssw *cfg_;
  HAPAccessoryServerRef *const server_;
  const HAPAccessory *const accessory_;

  HAPService svc_;
  Input::HandlerID handler_id_ = Input::kInvalidHandlerID;
  std::vector<std::unique_ptr<hap::Characteristic>> chars_;
  std::vector<HAPCharacteristic *> hap_chars_;

  Input::Event last_ev_ = Input::Event::kChange;
  double last_ev_ts_ = 0;

  StatelessSwitch(const StatelessSwitch &other) = delete;
};

}  // namespace hap
}  // namespace shelly
