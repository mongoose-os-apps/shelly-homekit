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

#include "mgos_gpio.h"
#include "mgos_onewire.h"

/* Model IDs */
#define DS18S20MODEL 0x10
#define DS18B20MODEL 0x28
#define DS1822MODEL 0x22
#define DS1825MODEL 0x3B
#define DS28EA00MODEL 0x42

#define CONVERT_T 0x44
#define READ_SCRATCHPAD 0xBE
#define READ_POWER_SUPPLY 0xB4

namespace shelly {

Onewire::Onewire(int pin_in, int pin_out) {
  pin_out_ = pin_out;
  ow_ = mgos_onewire_create_separate_io(pin_in, pin_out);
}

Onewire::~Onewire() {
  mgos_onewire_close(ow_);

  // release output pin
  mgos_gpio_setup_input(pin_out_, MGOS_GPIO_PULL_UP);
}

struct mgos_onewire *Onewire::Get() {
  return ow_;
}

std::vector<std::unique_ptr<TempSensor>> Onewire::DiscoverAll() {
  std::vector<std::unique_ptr<TempSensor>> sensors;
  mgos_onewire_search_clean(ow_);
  std::unique_ptr<TempSensor> sensor;
  while (sensor = NextAvailableSensor(0)) {
    sensors.push_back(std::move(sensor));
  }
  LOG(LL_INFO, ("Found %i sensors", sensors.size()));
  return sensors;
}

std::unique_ptr<TempSensor> Onewire::NextAvailableSensor(int type) {
  ROM rom;
  int mode = 0;
  std::unique_ptr<TempSensor> sensor;
  if (mgos_onewire_next(ow_, (uint8_t *) &rom, mode)) {
    uint8_t family = rom.family;
    if (TempSensorDS18XXX::SupportsFamily(family)) {
      sensor.reset(new TempSensorDS18XXX(ow_, rom));
      sensor->Init();
    } else {
      LOG(LL_INFO, ("Found unsupported device (family %u)", family));
    }
  }
  (void) type;
  return sensor;
}

TempSensorDS18XXX::TempSensorDS18XXX(struct mgos_onewire *ow,
                                     const Onewire::ROM &rom)
    : ow_(ow),
      rom_(rom),
      meas_timer_(std::bind(&TempSensorDS18XXX::UpdateTemperatureCB, this)),
      read_timer_(std::bind(&TempSensorDS18XXX::ReadTemperatureCB, this)) {
  result_ =
      mgos::Errorf(STATUS_UNAVAILABLE, "%llx: Not updated yet", rom_.serial);
}

TempSensorDS18XXX::~TempSensorDS18XXX() {
}

Status TempSensorDS18XXX::Init() {
  auto spr = ReadScratchpad();
  if (!spr.ok()) {
    result_ = spr.status();
    return spr.status();
  }
  const Scratchpad &sp = spr.ValueOrDie();

  LOG(LL_INFO, ("DS18XXX: model: %02X, sn: %llx, "
                "parasitic power: %s, resolution: %d",
                (uint8_t) rom_.family, (unsigned long long) rom_.serial,
                YesNo(ReadPowerSupply()), sp.GetResolution()));
  conversion_time_ms_ = sp.GetConversionTimeMs();
  return Status::OK();
}

void TempSensorDS18XXX::StartUpdating(int interval) {
  read_timer_.Clear();  // Clear eventually pending read
  meas_timer_.Reset(interval, MGOS_TIMER_REPEAT | MGOS_TIMER_RUN_NOW);
}

StatusOr<float> TempSensorDS18XXX::GetTemperature() {
  return result_;
}

bool TempSensorDS18XXX::SupportsFamily(uint8_t family) {
  if (family == DS18B20MODEL || family == DS18S20MODEL ||
      family == DS1822MODEL || family == DS1825MODEL ||
      family == DS28EA00MODEL) {
    return true;
  }
  return false;
}

StatusOr<TempSensorDS18XXX::Scratchpad> TempSensorDS18XXX::ReadScratchpad() {
  Scratchpad result;
  if (!mgos_onewire_reset(ow_)) {
    return mgos::Errorf(STATUS_UNAVAILABLE, "%llx: Bus reset failed",
                        rom_.serial);
  }
  mgos_onewire_select(ow_, (uint8_t *) &rom_);
  mgos_onewire_write(ow_, READ_SCRATCHPAD);
  mgos_onewire_read_bytes(ow_, (uint8_t *) &result, sizeof(result));
  uint8_t crc = mgos_onewire_crc8((uint8_t *) &result, sizeof(result) - 1);
  if (crc != result.crc) {
    return mgos::Errorf(STATUS_DATA_LOSS,
                        "%llx: Invalid scratchpad CRC: %#02x vs %#02x",
                        rom_.serial, crc, result.crc);
  }
  return result;
}

bool TempSensorDS18XXX::ReadPowerSupply() {
  if (!mgos_onewire_reset(ow_)) {
    return false;
  }
  mgos_onewire_select(ow_, (uint8_t *) &rom_);
  mgos_onewire_write(ow_, READ_POWER_SUPPLY);
  return (mgos_onewire_read_bit(ow_) == 0);
}

void TempSensorDS18XXX::ReadTemperatureCB() {
  auto spr = ReadScratchpad();
  if (!spr.ok()) {
    result_ = spr.status();
    LOG(LL_ERROR, ("%llx: failed to read scratchpad: %s", rom_.serial,
                   spr.status().ToString().c_str()));
    return;
  }
  const Scratchpad &sp = spr.ValueOrDie();
  if (rom_.family == DS18S20MODEL) {
    result_ = ((int16_t) (sp.temperature & 0xFFFE) / 2.0) - 0.25 +
              ((float) (sp.count_per_c - sp.count_remain) / sp.count_per_c);
  } else {
    result_ = sp.temperature * 0.0625f;
  }
  conversion_time_ms_ = sp.GetConversionTimeMs();
  if (notifier_) {
    notifier_();
  }
}

void TempSensorDS18XXX::UpdateTemperatureCB() {
  if (!mgos_onewire_reset(ow_)) {
    result_ =
        mgos::Errorf(STATUS_UNAVAILABLE, "%llx: Bus reset failed", rom_.serial);
    return;
  }
  mgos_onewire_select(ow_, (uint8_t *) &rom_);
  mgos_onewire_write(ow_, CONVERT_T);
  read_timer_.Reset(conversion_time_ms_, 0);
}

int TempSensorDS18XXX::Scratchpad::GetResolution() const {
  return (9 + cfg.resolution);
}

int TempSensorDS18XXX::Scratchpad::GetConversionTimeMs() const {
  switch (cfg.resolution) {
    case 0:
      return 94;
    case 1:
      return 188;
    case 2:
      return 375;
    default:
      return 750;
  }
}

}  // namespace shelly
