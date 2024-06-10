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

#include "shelly_dht_sensor.hpp"
#include <cmath>
#include "mgos_hal.h"

namespace shelly {

std::vector<std::unique_ptr<TempSensor>> DiscoverDHTSensors(int in, int out) {
  std::vector<std::unique_ptr<TempSensor>> sensors;

  std::unique_ptr<DHTSensor> dht(new DHTSensor(in, out));
  auto status = dht->Init();
  if (status == Status::OK()) {
    sensors.push_back(std::move(dht));
  } else {
    LOG(LL_ERROR, ("dht init failed: %s", status.ToString().c_str()));
  }
  return sensors;
}

DHTSensor::DHTSensor(uint8_t pin_in, uint8_t pin_out)
    : pin_in_(pin_in),
      pin_out_(pin_out),
      meas_timer_(std::bind(&DHTSensor::UpdateTemperatureCB, this)) {
  result_ = mgos::Errorf(STATUS_UNAVAILABLE, "Not updated yet");
  result_humidity_ = mgos::Errorf(STATUS_UNAVAILABLE, "Not updated yet");
}

DHTSensor::~DHTSensor() {
}

Status DHTSensor::Init() {
  dht = mgos_dht_create_separate_io(pin_in_, pin_out_, DHT21);

  if (dht == NULL) {
    return mgos::Errorf(STATUS_NOT_FOUND, "dht sensor init unsuccesfull");
  }

  size_t tries =
      2;  // first read was observed to be not successfull, reason unknown
  mgos_dht_stats stats;
  for (size_t i = 0; i < tries; i++) {
    result_ = mgos_dht_get_temp(dht);
    if (mgos_dht_getStats(dht, &stats)) {
      if (stats.read_success >= 1) {
        return Status::OK();
      } else if (i != (tries - 1)) {
        mgos_msleep(2 *
                    1000);  // try again after wait time of MGOs_DHT_READ_DELAY
      }
    }
  }

  return mgos::Errorf(STATUS_NOT_FOUND, "No DHT Sensor found");
}

void DHTSensor::StartUpdating(int interval) {
  meas_timer_.Reset(interval, MGOS_TIMER_REPEAT | MGOS_TIMER_RUN_NOW);
}

StatusOr<float> DHTSensor::GetTemperature() {
  return result_;
}

StatusOr<float> DHTSensor::GetHumidity() {
  return result_humidity_;
}

void DHTSensor::UpdateTemperatureCB() {
  float result = mgos_dht_get_temp(dht);
  float result_humidity = mgos_dht_get_humidity(dht);

  // check values for validity
  if (((result == 0) and (result_humidity == 0)) or isnan(result) or
      isnan(result_humidity)) {
    // error during readout do nothing for now
    LOG(LL_INFO, ("DHT: invalid value received"));
    return;
  }

  result_ = result;
  result_humidity_ = result_humidity;

  if (notifier_) {
    notifier_();
  }
  if (notifier_hum_) {
    notifier_hum_();
  }
}

}  // namespace shelly
