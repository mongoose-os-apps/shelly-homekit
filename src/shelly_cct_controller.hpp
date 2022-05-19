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

#include "mgos.hpp"
#include "mgos_hap_chars.hpp"
#include "mgos_timers.hpp"
#include "shelly_light_bulb_controller.hpp"
#include "shelly_output.hpp"

namespace shelly {

struct StateCCT {
  float ww;
  float cw;

  StateCCT operator+(const StateCCT &other) const {
    return {
        .ww = ww + other.ww,
        .cw = cw + other.cw,
    };
  }

  StateCCT operator*(float a) const {
    return {
        .ww = a * ww,
        .cw = a * cw,
    };
  }

  std::string ToString() const;
};

class CCTController : public LightBulbController<StateCCT> {
 public:
  CCTController(struct mgos_config_lb *cfg, Output *out_ww, Output *out_cw);
  CCTController(const CCTController &other) = delete;
  ~CCTController();

  BulbType Type() final {
    return BulbType::kCCT;
  }

 private:
  Output *const out_ww_, *const out_cw_;

  StateCCT ConfigToState(const struct mgos_config_lb &cfg) const final;
  void ReportTransition(const StateCCT &next, const StateCCT &prev) final;
  void UpdatePWM(const StateCCT &state) final;
};
}  // namespace shelly
