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

namespace shelly {

class OWTempSensor;

class Onewire {
 public:
  Onewire(int pin_in, int pin_out);
  ~Onewire();
  struct mgos_onewire *Get();
  std::vector<std::unique_ptr<TempSensor>> DiscoverAll(int num_sensors_max);

 private:
  struct mgos_onewire *ow_;
  std::unique_ptr<OWTempSensor> NextAvailableSensor(int type);
};

class OWTempSensor : public TempSensor {
 public:
  OWTempSensor(struct mgos_onewire *ow, uint8_t *rom);
  virtual ~OWTempSensor();

  virtual StatusOr<float> GetTemperature() override = 0;

  float cached_temperature_;
  uint8_t rom_[8];
  struct mgos_onewire *ow_;

  mgos::Timer meas_timer_;
  mgos::Timer read_timer_;

  virtual void StartUpdating(int interval) override;

 private:
  virtual void ReadTemperatureCB() = 0;
  virtual void UpdateTemperatureCB() = 0;
};

class TempSensorDS18XXX : public OWTempSensor {
 public:
  TempSensorDS18XXX(struct mgos_onewire *ow, uint8_t *rom);
  virtual ~TempSensorDS18XXX();
  static bool SupportsFamily(uint8_t family);

  StatusOr<float> GetTemperature() override;

 private:
  int ConversionTime();
  unsigned int GetResolution();
  unsigned int resolution_;

  virtual void ReadTemperatureCB() override;
  virtual void UpdateTemperatureCB() override;

  void ReadScratchpad(uint8_t *scratchpad, size_t len);
  bool VerifyScratchpad(uint8_t *scratchpad);
  bool ReadPowerSupply();
};

}  // namespace shelly
