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

#include "shelly_temp_sensor.hpp"

#include <vector>

#include <mgos_onewire.h>

#include "mgos_timers.hpp"

struct scratchpad;
struct rom;

namespace shelly {

class Onewire {
 public:
  Onewire(int pin_in, int pin_out);
  ~Onewire();
  struct mgos_onewire *Get();
  std::vector<std::unique_ptr<TempSensor>> DiscoverAll();

 private:
  struct mgos_onewire *ow_;

  int pin_out_;
  std::unique_ptr<TempSensor> NextAvailableSensor(int type);
};

class TempSensorDS18XXX : public TempSensor {
 public:
  TempSensorDS18XXX(struct mgos_onewire *ow, struct rom *rom);
  virtual ~TempSensorDS18XXX();

  StatusOr<float> GetTemperature() override;

  static bool SupportsFamily(uint8_t family);
  virtual void StartUpdating(int interval) override;

 private:
  float cached_temperature_;
  struct rom *rom_;
  struct mgos_onewire *ow_;

  mgos::Timer meas_timer_;
  mgos::Timer read_timer_;

  int ConversionTime();
  unsigned int GetResolution();
  unsigned int resolution_;

  virtual void ReadTemperatureCB();
  virtual void UpdateTemperatureCB();

  void ReadScratchpad(struct scratchpad *scratchpad);
  bool VerifyScratchpad(struct scratchpad *scratchpad);
  bool ReadPowerSupply();
};

}  // namespace shelly
