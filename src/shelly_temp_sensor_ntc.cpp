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

#include "shelly_temp_sensor_ntc.hpp"

#ifdef MGOS_HAVE_ADC

#include <cmath>

#include "mgos_adc.h"

#define ADC_RESOLUTION 1024.0f

namespace shelly {

NTCTempSensor::NTCTempSensor(int adc_channel, float vin, float rd)
    : adc_channel_(adc_channel), vin_(vin), rd_(rd) {
  mgos_adc_enable(adc_channel);
}

NTCTempSensor::~NTCTempSensor() {
}

StatusOr<float> NTCTempSensor::GetTemperature() {
  int raw = mgos_adc_read(adc_channel_);
  float v_out = raw / ADC_RESOLUTION;
  float rt = (v_out * rd_) / (vin_ - v_out);
  // LOG(LL_INFO, ("RAW: %d, v_out %.3f rt %.3f", raw, v_out, rt));
  return Interpolate(rt);
}

float NTCTempSensor::Interpolate(float rt) {
  float t;
  const CurveDataPoint *e1 = GetCurve(), *e2 = e1;
  do {
    if (rt >= e2->r) break;
    e1 = e2;
    e2++;
    if (e2->r == 0) e2--;
  } while (e1 != e2);
  if (e1 != e2) {
    // log10 interpolation.
    float f = log10f(rt / e2->r) / log10f(e1->r / e2->r);
    t = e2->t - (e2->t - e1->t) * f;
  } else {
    t = e1->t;
  }
  // LOG(LL_INFO, ("rt %.3f e1 %.3f:%.3f e2 %.3f:%.3f t %.3f", rt, e1->r, e1->t,
  //               e2->r, e2->t, t));
  return t;
}

TempSensorSDNT1608X103F3450::TempSensorSDNT1608X103F3450(int adc_channel,
                                                         float vin, float rd)
    : NTCTempSensor(adc_channel, vin, rd) {
}

TempSensorSDNT1608X103F3450 ::~TempSensorSDNT1608X103F3450() {
}

const NTCTempSensor::CurveDataPoint *TempSensorSDNT1608X103F3450::GetCurve() {
  // clang-format off
  static const CurveDataPoint s_SDNT1608X103F3450_curve[] = {
      {200000, -39},
      {150000, -33},
      {100000, -26},
      {90000, -25},
      {80000, -22},
      {70000, -19},
      {60000, -16},
      {55000, -14},
      {50000, -13},
      {45000, -10},
      {40000, -8},
      {35000, -5},
      {30000, -2},
      {25000, 2},
      {20000, 8},
      {15000, 13},
      {10000, 25},
      {9000, 27},
      {8000, 31},
      {7000, 34},
      {6000, 38.5},
      {5000, 44.5},
      {4500, 46.5},
      {4000, 51.5},
      {3500, 54},
      {3000, 59},
      {2500, 64},
      {2000, 72.5},
      {1500, 82},
      {1000, 98},
      {900, 103},
      {800, 108},
      {0, 0},
  };
  // clang-format on
  return s_SDNT1608X103F3450_curve;
}

}  // namespace shelly

#endif  // MGOS_HAVE_ADC
