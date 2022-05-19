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

#include "mgos_sys_config.h"
#include "mgos_timers.hpp"
#include "shelly_common.hpp"

namespace shelly {

class LightBulbControllerBase;

class LightEffect {
 public:
  virtual ~LightEffect(){};

  virtual Status Start() = 0;
};

class LightEffectBlink : public LightEffect {
 public:
  LightEffectBlink(LightBulbControllerBase *bulb, int interval_ms,
                   int repeat_n);
  ~LightEffectBlink() override{};
  Status Start() override;

 private:
  int const interval_;
  const LightBulbControllerBase *bulb_;

  mgos::Timer repeat_timer_;

  // effect state
  int repeat_n_;  // counter. -1 is infinity, stops on zero
  bool active_;

  struct mgos_config_lb cfg;
  void TimerCB();
};

}  // namespace shelly