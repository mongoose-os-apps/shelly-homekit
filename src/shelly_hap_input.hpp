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

#include "HAP.h"
#include "mgos_sys_config.h"

#include "shelly_common.hpp"
#include "shelly_component.hpp"
#include "shelly_hap_accessory.hpp"
#include "shelly_hap_service.hpp"
#include "shelly_input.hpp"

namespace shelly {
namespace hap {

// A service that represents a single Shelly input.
// Internally it instantiates either a Stateless Switch or Motion Sensor
// and forwards API calls to it.
class ShellyInput : public Component {
 public:
  enum class ServiceType {
    kDisabled = 0,
    kStatelessSwitch = 1,
    kMotionSensor = 2,
    kOccupancySensor = 3,
    kMax,
  };

  ShellyInput(int id, Input *in, struct mgos_config_in *cfg);
  virtual ~ShellyInput();

  // Component interface impl.
  Type type() const override;
  Status Init() override;
  std::string name() const override;
  StatusOr<std::string> GetInfo() const override;
  StatusOr<std::string> GetInfoJSON() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;

  uint16_t GetAIDBase() const;
  Service *GetService() const;

 private:
  Input *const in_;
  struct mgos_config_in *cfg_;
  ServiceType svc_type_;  // Service type at creation.

  std::unique_ptr<Component> c_;
  Service *s_ = nullptr;

  ShellyInput(const ShellyInput &other) = delete;
};

void CreateHAPInput(int id, const struct mgos_config_in *cfg,
                    std::vector<std::unique_ptr<Component>> *comps,
                    std::vector<std::unique_ptr<hap::Accessory>> *accs,
                    HAPAccessoryServerRef *svr);

}  // namespace hap
}  // namespace shelly
