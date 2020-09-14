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

#include "HAP.h"

#include "shelly_common.h"

#define IID_BASE_SWITCH 0x100
#define IID_STEP_SWITCH 4
#define IID_BASE_OUTLET 0x200
#define IID_STEP_OUTLET 5
#define IID_BASE_LOCK 0x300
#define IID_STEP_LOCK 4
#define IID_BASE_STATELESS_SWITCH 0x400
#define IID_STEP_STATELESS_SWITCH 4

namespace shelly {
namespace hap {

class Service {
 public:
  Service() {
  }
  virtual ~Service() {
  }
  virtual Status Init() = 0;
  virtual const HAPService *GetHAPService() const = 0;

 private:
  Service(const Service &other) = delete;
};

}  // namespace hap
}  // namespace shelly
