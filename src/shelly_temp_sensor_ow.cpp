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

#ifdef MGOS_HAVE_ONEWIRE

#include <cmath>

#include <mgos_onewire.h>

/* Model IDs */
#define DS18S20MODEL 0x10 /* also DS1820 */
#define DS18B20MODEL 0x28
#define DS1822MODEL 0x22
#define DS1825MODEL 0x3B

/* Scratchpad size */
#define SCRATCHPAD_SIZE 9

/* Scratchpad locations */
#define CONFIGURATION 4
#define SCRATCHPAD_CRC 8

/* Device resolution */
#define TEMP_9_BIT 0x1F  /* 9 bit */
#define TEMP_10_BIT 0x3F /* 10 bit */
#define TEMP_11_BIT 0x5F /* 11 bit */
#define TEMP_12_BIT 0x7F /* 12 bit */

#define CONVERT_T 0x44
#define READ_SCRATCHPAD 0xBE
#define READ_POWER_SUPPLY 0xB4

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

OWSensorManager::OWSensorManager(Onewire *ow) {
  ow_ = ow->Get();
}

void OWSensorManager::DiscoverAll(
    int num_sensors_max, std::vector<std::unique_ptr<TempSensor>> *sensors) {
  mgos_onewire_search_clean(ow_);
  int num_sensors = 0;
  std::unique_ptr<TempSensor> sensor;
  while ((sensor = NextAvailableSensor(0)) && (num_sensors < num_sensors_max)) {
    sensors->push_back(std::move(sensor));
    num_sensors++;
  }
  LOG(LL_INFO, ("Discovered %i sensors", num_sensors));
}

std::unique_ptr<OWTempSensor> OWSensorManager::NextAvailableSensor(int type) {
  uint8_t rom[8] = {0};
  int mode = 0;
  std::unique_ptr<OWTempSensor> sensor;
  if (mgos_onewire_next(ow_, rom, mode)) {
    uint8_t family = rom[0];
    if (TempSensorDS18XXX::SupportsFamily(family)) {
      sensor.reset(new TempSensorDS18XXX(ow_, rom));
    } else {
      LOG(LL_INFO, ("Found non-supported device"));
    }
  }
  (void) type;
  return sensor;
}

OWSensorManager::~OWSensorManager() {
}

OWTempSensor::OWTempSensor(struct mgos_onewire *ow, uint8_t *rom)
    : cached_temperature_(0),
      meas_timer_(std::bind(&OWTempSensor::UpdateTemperatureCB, this)),
      read_timer_(std::bind(&OWTempSensor::ReadTemperatureCB, this)) {
  memcpy(rom_, rom, 8);
  ow_ = ow;
}

OWTempSensor::~OWTempSensor() {
}

void OWTempSensor::StartUpdating(int interval) {
  meas_timer_.Reset(interval, MGOS_TIMER_REPEAT | MGOS_TIMER_RUN_NOW);
}

TempSensorDS18XXX::TempSensorDS18XXX(struct mgos_onewire *ow, uint8_t *rom)
    : OWTempSensor(ow, rom) {
  resolution_ = GetResolution();
  uint64_t serial_mask = 0xFFFFFFFFFFFF;
  uint64_t serial_ = ((*(uint64_t *) rom) >> 8) & serial_mask;
  LOG(LL_INFO, ("DS18XXX: model: %02X, serial number: %" PRIx64
                ", resolution: %d, parasitic power: %d",
                rom[0], serial_, resolution_, ReadPowerSupply()));
}

TempSensorDS18XXX ::~TempSensorDS18XXX() {
}

StatusOr<float> TempSensorDS18XXX::GetTemperature() {
  return cached_temperature_;
}

bool TempSensorDS18XXX::SupportsFamily(uint8_t family) {
  if (family == DS18B20MODEL or family == DS18S20MODEL or
      family == DS1822MODEL or family == DS1825MODEL) {
    return true;
  }
  return false;
}

void TempSensorDS18XXX::ReadScratchpad(uint8_t *scratchpad, size_t len) {
  if (!mgos_onewire_reset(ow_)) {
    return;
  }
  mgos_onewire_select(ow_, rom_);
  mgos_onewire_write(ow_, READ_SCRATCHPAD);
  mgos_onewire_read_bytes(ow_, scratchpad, len);
}

bool TempSensorDS18XXX::ReadPowerSupply() {
  if (!mgos_onewire_reset(ow_)) {
    return false;
  }
  mgos_onewire_select(ow_, rom_);
  mgos_onewire_write(ow_, READ_POWER_SUPPLY);
  return (mgos_onewire_read_bit(ow_) == 0);
}

bool TempSensorDS18XXX::VerifyScratchpad(uint8_t *scratchpad) {
  return mgos_onewire_crc8(scratchpad, 8) == scratchpad[SCRATCHPAD_CRC];
}

void TempSensorDS18XXX::ReadTemperatureCB() {
  uint8_t temp[SCRATCHPAD_SIZE];
  ReadScratchpad(temp, SCRATCHPAD_SIZE);
  if (VerifyScratchpad(temp)) {
    uint16_t temp_s = temp[1] << 8 | temp[0];
    float temperature = temp_s * 0.0625;
    LOG(LL_INFO, ("Read temperature %f", temperature));
    cached_temperature_ = temperature;
    if (notifier_) {
      notifier_();
    }
  }
}

void TempSensorDS18XXX::UpdateTemperatureCB() {
  LOG(LL_INFO, ("Update temperature"));
  if (!mgos_onewire_reset(ow_)) {
    return;
  }
  mgos_onewire_select(ow_, rom_);
  mgos_onewire_write(ow_, CONVERT_T);
  int time = ConversionTime();
  read_timer_.Reset(time, 0);
}

unsigned int TempSensorDS18XXX::GetResolution() {
  uint8_t scratchpad[SCRATCHPAD_SIZE] = {0};
  ReadScratchpad(scratchpad, SCRATCHPAD_SIZE);
  if (!VerifyScratchpad(scratchpad)) {
    return 0;
  }
  switch (scratchpad[CONFIGURATION]) {
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

#endif  // MGOS_HAVE_OW
