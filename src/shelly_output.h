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

#include "shelly_common.h"

namespace shelly {

class Output {
 public:
  virtual bool GetState() = 0;
  virtual Status SetState(bool on, const char *source) = 0;
};

class PowerMeter {
 public:
  virtual StatusOr<float> GetPowerW() = 0;
  virtual StatusOr<float> GetEnergyWH() = 0;
};

class OutputPin : public Output {
 public:
  OutputPin(int id, int pin, bool on_value, bool initial_state);
  virtual ~OutputPin();

  // Output interface impl.
  bool GetState() override;
  Status SetState(bool on, const char *source) override;

 private:
  const int id_;
  const int pin_;
  const bool on_value_;

  OutputPin(const OutputPin &other) = delete;
};

}  // namespace shelly
