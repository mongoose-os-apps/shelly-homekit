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

#include "mgos_timers.hpp"

struct mgos_onewire;

namespace shelly {

class Onewire {
 public:
  struct ROM {
    uint64_t family : 8;
    uint64_t serial : 48;
    uint64_t crc : 8;

    ROM() : family(0), serial(0), crc(0) {
    }
  };

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
  TempSensorDS18XXX(struct mgos_onewire *ow, const Onewire::ROM &rom);
  virtual ~TempSensorDS18XXX();

  Status Init();
  StatusOr<float> GetTemperature() override;

  static bool SupportsFamily(uint8_t family);
  virtual void StartUpdating(int interval) override;

 private:
  struct __attribute__((__packed__)) Scratchpad {
    int16_t temperature;
    uint8_t th;
    uint8_t tl;
    union {
      uint8_t rsvd_1 : 5;
      uint8_t resolution : 2;
      uint8_t rsvd_0 : 1;
      uint8_t val;
    } cfg;
    uint8_t rfu;
    uint8_t count_remain;
    uint8_t count_per_c;
    uint8_t crc;

    int GetResolution() const;
    int GetConversionTimeMs() const;
  };

  void ReadTemperatureCB();
  void UpdateTemperatureCB();

  StatusOr<Scratchpad> ReadScratchpad();
  bool ReadPowerSupply();

  struct mgos_onewire *ow_ = nullptr;
  const Onewire::ROM rom_;
  StatusOr<float> result_;

  mgos::Timer meas_timer_;
  mgos::Timer read_timer_;
  int conversion_time_ms_ = 750;
};

}  // namespace shelly
