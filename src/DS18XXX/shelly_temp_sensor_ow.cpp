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

#include "shelly_temp_sensor_ow.hpp"

#include <cmath>

#include "mgos_onewire.h"

/* Model IDs */
#define DS18S20MODEL 0x10
#define DS18B20MODEL 0x28
#define DS1822MODEL 0x22
#define DS1825MODEL 0x3B
#define DS28EA00MODEL 0x42

/* Device resolution */
#define TEMP_9_BIT 0x1F  /* 9 bit */
#define TEMP_10_BIT 0x3F /* 10 bit */
#define TEMP_11_BIT 0x5F /* 11 bit */
#define TEMP_12_BIT 0x7F /* 12 bit */

#define CONVERT_T 0x44
#define READ_SCRATCHPAD 0xBE
#define READ_POWER_SUPPLY 0xB4

struct __attribute__((__packed__)) scratchpad {
  int16_t temperature;
  uint8_t th;
  uint8_t tl;
  uint8_t configuration;
  uint8_t rfu;
  uint8_t count_remain;
  uint8_t count_per_c;
  uint8_t crc;
};

struct __attribute__((__packed__)) rom {
  uint8_t family;
  uint64_t serial;
  uint8_t crc;
};

namespace shelly {

Onewire::Onewire(int pin_in, int pin_out) {
  ow_ = mgos_onewire_create_separate_io(pin_in, pin_out);
}

Onewire::~Onewire() {
  mgos_onewire_close(ow_);
}

struct mgos_onewire *Onewire::Get() {
  return ow_;
}

std::vector<std::unique_ptr<TempSensor>> Onewire::DiscoverAll(
    int num_sensors_max) {
  std::vector<std::unique_ptr<TempSensor>> sensors;
  mgos_onewire_search_clean(ow_);
  int num_sensors = 0;
  std::unique_ptr<TempSensor> sensor;
  while ((sensor = NextAvailableSensor(0)) && (num_sensors < num_sensors_max)) {
    sensors.push_back(std::move(sensor));
    num_sensors++;
  }
  LOG(LL_INFO, ("Discovered %i sensors", num_sensors));
  return sensors;
}

std::unique_ptr<TempSensor> Onewire::NextAvailableSensor(int type) {
  struct rom rom = {0};
  int mode = 0;
  std::unique_ptr<TempSensor> sensor;
  if (mgos_onewire_next(ow_, (uint8_t *) &rom, mode)) {
    uint8_t family = rom.family;
    if (TempSensorDS18XXX::SupportsFamily(family)) {
      sensor.reset(new TempSensorDS18XXX(ow_, &rom));
    } else {
      LOG(LL_INFO, ("Found non-supported device"));
    }
  }
  (void) type;
  return sensor;
}

void TempSensorDS18XXX::StartUpdating(int interval) {
  meas_timer_.Reset(interval, MGOS_TIMER_REPEAT | MGOS_TIMER_RUN_NOW);
}

TempSensorDS18XXX::TempSensorDS18XXX(struct mgos_onewire *ow,
                                     struct rom *rom_content)
    : cached_temperature_(0),
      meas_timer_(std::bind(&TempSensorDS18XXX::UpdateTemperatureCB, this)),
      read_timer_(std::bind(&TempSensorDS18XXX::ReadTemperatureCB, this)) {
  rom_ = new rom();
  memcpy(rom_, rom_content, sizeof(struct rom));
  ow_ = ow;
  resolution_ = GetResolution();
  LOG(LL_INFO, ("DS18XXX: model: %02X, serial number: %" PRIx64
                ", resolution: %d, parasitic power: %d",
                rom_->family, rom_->serial, resolution_, ReadPowerSupply()));
}

TempSensorDS18XXX ::~TempSensorDS18XXX() {
  delete rom_;
}

StatusOr<float> TempSensorDS18XXX::GetTemperature() {
  return cached_temperature_;
}

bool TempSensorDS18XXX::SupportsFamily(uint8_t family) {
  if (family == DS18B20MODEL or family == DS18S20MODEL or
      family == DS1822MODEL or family == DS1825MODEL or
      family == DS28EA00MODEL) {
    return true;
  }
  return false;
}

void TempSensorDS18XXX::ReadScratchpad(struct scratchpad *scratchpad) {
  if (!mgos_onewire_reset(ow_)) {
    return;
  }
  mgos_onewire_select(ow_, (uint8_t *) rom_);
  mgos_onewire_write(ow_, READ_SCRATCHPAD);
  mgos_onewire_read_bytes(ow_, (uint8_t *) scratchpad,
                          sizeof(struct scratchpad));
}

bool TempSensorDS18XXX::ReadPowerSupply() {
  if (!mgos_onewire_reset(ow_)) {
    return false;
  }
  mgos_onewire_select(ow_, (uint8_t *) rom_);
  mgos_onewire_write(ow_, READ_POWER_SUPPLY);
  return (mgos_onewire_read_bit(ow_) == 0);
}

bool TempSensorDS18XXX::VerifyScratchpad(struct scratchpad *scratchpad) {
  return mgos_onewire_crc8((uint8_t *) scratchpad,
                           sizeof(struct scratchpad) - 1) == scratchpad->crc;
}

void TempSensorDS18XXX::ReadTemperatureCB() {
  struct scratchpad temp = {0};
  float temperature = 0;
  ReadScratchpad(&temp);
  if (VerifyScratchpad(&temp)) {
    int16_t temp_s = temp.temperature;
    if (rom_->family == DS18S20MODEL) {
      temperature =
          ((int16_t)(temp_s & 0xFFFE) / 2.0) - 0.25 +
          ((float) (temp.count_per_c - temp.count_remain) / temp.count_per_c);
    } else {
      temperature = temp_s * 0.0625;
    }
    LOG(LL_INFO, ("Read temperature %f", temperature));
    cached_temperature_ = temperature;
    if (notifier_) {
      notifier_();
    }
  }
}

void TempSensorDS18XXX::UpdateTemperatureCB() {
  if (!mgos_onewire_reset(ow_)) {
    return;
  }
  mgos_onewire_select(ow_, (uint8_t *) rom_);
  mgos_onewire_write(ow_, CONVERT_T);
  int time = ConversionTime();
  read_timer_.Reset(time, 0);
}

unsigned int TempSensorDS18XXX::GetResolution() {
  struct scratchpad scratchpad = {0};
  ReadScratchpad(&scratchpad);
  if (!VerifyScratchpad(&scratchpad)) {
    return 0;
  }
  switch (scratchpad.configuration) {
    case TEMP_12_BIT:
      return 12;
    case TEMP_11_BIT:
      return 11;
    case TEMP_10_BIT:
      return 10;
    case TEMP_9_BIT:
      return 9;
  }
  return 0;
}

int TempSensorDS18XXX::ConversionTime() {
  switch (resolution_) {
    case 9:
      return 94;
    case 10:
      return 188;
    case 11:
      return 375;
    default:
      return 750;
  }
}

}  // namespace shelly
