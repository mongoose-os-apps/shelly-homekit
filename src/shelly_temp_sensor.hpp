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

#include "shelly_common.hpp"

namespace shelly {

typedef enum {
  TS = 0,
  TS_HUM,
} TempType;

class TempSensor {
 public:
  typedef std::function<void()> Notifier;

  TempSensor();
  virtual ~TempSensor();
  TempSensor(const TempSensor &other) = delete;

  virtual Status Init() = 0;

  virtual StatusOr<float> GetTemperature() = 0;

  virtual void StartUpdating(int interval UNUSED_ARG) {
  }

  virtual TempType getType() {
    return TS;
  }

  void SetNotifier(Notifier notifier);

 protected:
  Notifier notifier_;
};

class HumidityTempSensor : public TempSensor {
 public:
  virtual StatusOr<float> GetHumidity() = 0;
  void SetNotifierHumidity(Notifier notifier);
  TempType getType() {
    return TS_HUM;
  }

 protected:
  Notifier notifier_hum_;
};

}  // namespace shelly
