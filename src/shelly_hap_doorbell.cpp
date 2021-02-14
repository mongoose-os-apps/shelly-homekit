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

#include "shelly_hap_doorbell.hpp"

#include "HAPUUID.h"

namespace shelly {
namespace hap {

const HAPUUID kHAPServiceType_Doorbell = HAPUUIDCreateAppleDefined(0x121);

Doorbell::Doorbell(int id, Input *in, struct mgos_config_in_ssw *cfg)
    : StatelessSwitchBase(id, in, cfg, SHELLY_HAP_IID_BASE_DOORBELL,
                          &kHAPServiceType_Doorbell, "service.doorbell") {
}

Doorbell::~Doorbell() {
}

Component::Type Doorbell::type() const {
  return Type::kDoorbell;
}

}  // namespace hap
}  // namespace shelly