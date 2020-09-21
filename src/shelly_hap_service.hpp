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

#include <vector>

#include "HAP.h"

#include "shelly_common.hpp"
#include "shelly_hap_chars.hpp"

#define SHELLY_HAP_IID_BASE_SWITCH 0x100
#define SHELLY_HAP_IID_STEP_SWITCH 4
#define SHELLY_HAP_IID_BASE_OUTLET 0x200
#define SHELLY_HAP_IID_STEP_OUTLET 5
#define SHELLY_HAP_IID_BASE_LOCK 0x300
#define SHELLY_HAP_IID_STEP_LOCK 4
#define SHELLY_HAP_IID_BASE_STATELESS_SWITCH 0x400
#define SHELLY_HAP_IID_STEP_STATELESS_SWITCH 4
#define SHELLY_HAP_IID_BASE_WINDOW_COVERING 0x400
#define SHELLY_HAP_IID_STEP_WINDOW_COVERING 0x10
#define SHELLY_HAP_IID_BASE_SERVICE_LABEL 0x1030

namespace shelly {
namespace hap {

class Accessory;

class Service {
 public:
  Service();
  Service(uint16_t iid, const HAPUUID *type, const char *debug_description,
          bool hidden = false);
  virtual ~Service();

  uint16_t iid() const;

  bool primary() const;
  void set_primary(bool is_primary);

  const Accessory *parent() const;
  void set_parent(const Accessory *parent);

  void AddChar(Characteristic *ch);  // Takes ownership of ch.

  void AddNameChar(uint16_t iid, const std::string &name);

  void AddLink(uint16_t iid);

  const HAPService *GetHAPService() const;

 protected:
  HAPService svc_;
  std::vector<std::unique_ptr<Characteristic>> chars_;
  std::vector<const HAPCharacteristic *> hap_chars_;
  std::vector<uint16_t> links_;

 private:
  const Accessory *parent_ = nullptr;

  Service(const Service &other) = delete;
};

class ServiceLabelService : public Service {
 public:
  explicit ServiceLabelService(uint8_t ns);
  virtual ~ServiceLabelService();
};

}  // namespace hap
}  // namespace shelly
