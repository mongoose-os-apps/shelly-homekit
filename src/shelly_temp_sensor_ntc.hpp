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

namespace shelly {

class NTCTempSensor : public TempSensor {
 public:
  NTCTempSensor(int adc_channel, float vin, float rd);
  virtual ~NTCTempSensor();

  Status Init() override;
  StatusOr<float> GetTemperature() override;

 protected:
  struct CurveDataPoint {
    float r;  // Ohm
    float t;  // Celsius
  };

  virtual const CurveDataPoint *GetCurve() = 0;

  float Interpolate(float rt);

 private:
  const int adc_channel_;
  const float vin_, rd_;
};

class TempSensorSDNT1608X103F3950 : public NTCTempSensor {
 public:
  TempSensorSDNT1608X103F3950(int adc_channel, float vin, float rd);
  virtual ~TempSensorSDNT1608X103F3950();

 protected:
  virtual const CurveDataPoint *GetCurve() override;
};

}  // namespace shelly
