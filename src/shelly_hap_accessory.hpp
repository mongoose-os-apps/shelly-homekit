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
#include "shelly_hap_service.hpp"

#define SHELLY_HAP_AID_PRIMARY 0x1
#define SHELLY_HAP_AID_BASE_SWITCH 0x100
#define SHELLY_HAP_AID_BASE_OUTLET 0x200
#define SHELLY_HAP_AID_BASE_LOCK 0x300
#define SHELLY_HAP_AID_BASE_STATELESS_SWITCH 0x400
#define SHELLY_HAP_AID_BASE_WINDOW_COVERING 0x500

namespace shelly {
namespace hap {

class Accessory {
 public:
  typedef std::function<HAPError(const HAPAccessoryIdentifyRequest *req)>
      IdentifyCB;

  Accessory(uint64_t aid, HAPAccessoryCategory category,
            const std::string &name, const IdentifyCB &identify_cb,
            HAPAccessoryServerRef *server = nullptr);
  virtual ~Accessory();

  HAPAccessoryServerRef *server() const;
  void set_server(HAPAccessoryServerRef *server);

  void SetCategory(HAPAccessoryCategory category);

  void AddService(std::unique_ptr<Service> svc);
  void AddHAPService(const HAPService *svc);

  const HAPAccessory *GetHAPAccessory() const;

 private:
  static HAPError Identify(HAPAccessoryServerRef *server,
                           const HAPAccessoryIdentifyRequest *request,
                           void *context);

  const std::string name_;
  const IdentifyCB identify_cb_;
  HAPAccessoryServerRef *server_;

  struct HAPAccessoryWithInstance {
    HAPAccessory acc;
    Accessory *inst;
  } hai_;

  std::vector<std::unique_ptr<Service>> svcs_;
  std::vector<const HAPService *> hap_svcs_;

  Accessory(const Accessory &other) = delete;
};

}  // namespace hap
}  // namespace shelly
