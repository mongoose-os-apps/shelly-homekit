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

#include "mgos.hpp"
#include "mgos_hap_chars.hpp"

#include "HAP+Internal.h"

namespace shelly {
namespace hap {

class LightBulb;

typedef uint16_t iidType;

typedef struct {
  float adjustmentFactor;
  float value;
  uint32_t offset;
  uint32_t duration;
  bool durationPresent;
} transitionEntryType;

typedef struct {
  int32_t minimumAdjustmentMultiplier;
  int32_t maximumAdjustmentMultiplier;
} adjustmentMultiplierRangeType;

typedef std::vector<transitionEntryType> curveVectorType;

typedef struct {
  curveVectorType *vec;
  size_t count;
} tableIterationContext;

typedef struct {
  HAPError (*enumerate)(HAPSequenceTLVDataSourceRef *dataSource,
                        HAPSequenceTLVEnumerateCallback callback,
                        void *context);
  HAPSequenceTLVDataSourceRef dataSource;
  transitionEntryType _;
} transitionCurveType;

typedef struct {
  transitionCurveType curve;
  adjustmentMultiplierRangeType adjustmentMultiplierRange;
  iidType iid;
  bool curvePresent;
} transitionCurveConfigurationType;

typedef HAPDataTLVValue uuidType;

typedef struct {
  uint64_t startTime;
  uint64_t id3;
  uuidType transitionId;
} parametersType;

typedef struct {  // sorted by size for reduced size due to alignment
  parametersType parameters;
  transitionCurveConfigurationType transitionCurveConfiguration;
  uint32_t notifyIntervalThreshold;
  uint16_t updateInterval;
  uint16_t unknown_7;
  iidType iid;
  uint8_t unknown_3;
  uint8_t unknown_4;
  bool unknown_4Present;
  bool unknown_3Present;
  bool parametersPresent;
  bool notifyIntervalThresholdPresent;
  bool unknown_7Present;
  bool updateIntervalPresent;
  bool transitionCurveConfigurationPresent;
} transitionType;

typedef struct {
  iidType iid;
} readTransitionType;

typedef struct {
  HAPError (*enumerate)(HAPSequenceTLVDataSourceRef *dataSource,
                        HAPSequenceTLVEnumerateCallback callback,
                        void *context);
  HAPSequenceTLVDataSourceRef dataSource;
  transitionType _;
} valueList;

typedef struct {
  valueList value;
} updateTransitionType;

typedef struct {
  transitionType *type;
  size_t count;
} transitionTypeIterationContext;

typedef struct {
  parametersType parameters;
  uint32_t timeSinceStart;
  iidType iid;
} configurationStatus;

typedef struct {
  HAPError (*enumerate)(HAPSequenceTLVDataSourceRef *dataSource,
                        HAPSequenceTLVEnumerateCallback callback,
                        void *context);
  HAPSequenceTLVDataSourceRef dataSource;
  configurationStatus _;
} updateTransitionResponseList;

typedef struct {
  updateTransitionType readTransitionResponse;
  updateTransitionResponseList updateTransitionResponse;
  bool readTransitionResponsePresent;
  bool updateTransitionResponsePresent;
} transitionControlTypeResponse;

typedef struct {
  readTransitionType readTransition;
  updateTransitionType updateTransition;
  bool readTransitionPresent;
  bool updateTransitionPresent;
} transitionControlTypeRequest;

typedef struct {
  iidType iid;
  uint8_t type;
} supportedConfig;

typedef struct {
  HAPError (*enumerate)(HAPSequenceTLVDataSourceRef *dataSource,
                        HAPSequenceTLVEnumerateCallback callback,
                        void *context);
  HAPSequenceTLVDataSourceRef dataSource;
  supportedConfig _;
} supportedConfigList;

class AdaptiveLighting {
 public:
  AdaptiveLighting(LightBulb *bulb, struct mgos_config_lb *cfg);
  virtual ~AdaptiveLighting();

  void ColorTempChangedManually();
  void BrightnessChangedManually();

  mgos::Status Init();

 private:
  void UpdateCB();
  void Disable();
  void AdjustColorTemp(uint16_t elapsed_time);

  LightBulb *bulb_;
  struct mgos_config_lb *cfg_;

  mgos::hap::TLV8Characteristic *transition_configuration_characteristic_ =
      nullptr;
  mgos::hap::TLV8Characteristic *transition_control_characteristic_ = nullptr;
  mgos::hap::UInt8Characteristic *transition_count_characteristic_ = nullptr;

  uint8_t active_transition_count_;  // we only support 1 transition

  transitionType active_transition_;

  curveVectorType active_table_;
  uint8_t active_transition_id_[16];

  uint32_t offset_millis_;
  uint32_t notification_millis_;

  mgos::Timer update_timer_;

  bool direct_answer_read_ = false;
  bool direct_answer_update_ = false;
};

}  // namespace hap
}  // namespace shelly
