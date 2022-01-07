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

#include <string>

#include "HAP.h"

#include "shelly_common.hpp"

namespace shelly {

struct OTAProgress {
  std::string version;
  std::string build;
  int progress_pct = -1;

  OTAProgress() = default;
  OTAProgress(const std::string &version, const std::string &build);
};
StatusOr<OTAProgress> GetOTAProgress();

void OTAInit(HAPAccessoryServerRef *server);

}  // namespace shelly
