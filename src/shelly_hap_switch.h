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

#include <vector>

#include "mgos_sys_config.h"

#include "shelly_common.h"
#include "shelly_component.h"
#include "shelly_hap.h"
#include "shelly_hap_chars.h"
#include "shelly_input.h"
#include "shelly_output.h"

namespace shelly {

class HAPSwitch : public Component, Service {
 public:
  HAPSwitch(Input *in, Output *out, PowerMeter *out_pm,
            const struct mgos_config_sw *cfg);
  virtual ~HAPSwitch();

  StatusOr<std::string> GetInfo() const override;
  const HAPService *GetHAPService() const override;

 private:
  void InputEventHandler(Input::Event ev, bool state);

  Input *in_;
  Output *out_;
  PowerMeter *out_pm_;
  const struct mgos_config_sw *cfg_;

  std::vector<std::unique_ptr<const ShellyHAPCharacteristic>> chars_;

  HAPService svc_;

  HAPSwitch(const HAPSwitch &other) = delete;
};

}  // namespace shelly
