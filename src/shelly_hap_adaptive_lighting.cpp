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

#include "shelly_hap_adaptive_lighting.hpp"

#include "shelly_hap_light_bulb.hpp"

// not officially documented, reverse engineering by HomeBridge
// https://github.com/homebridge/HAP-NodeJS/

enum class SupportedCharacteristicValueTransitionConfigurationsTypes {
  SUPPORTED_TRANSITION_CONFIGURATION = 0x01,
};

enum class SupportedValueTransitionConfigurationTypes {
  CHARACTERISTIC_IID = 0x01,
  TRANSITION_TYPE = 0x02,
};

enum class TransitionType {
  BRIGHTNESS = 0x01,
  COLOR_TEMPERATURE = 0x02,
};

enum class TransitionControlTypes {
  READ_CURRENT_VALUE_TRANSITION_CONFIGURATION = 0x01,
  UPDATE_VALUE_TRANSITION_CONFIGURATION = 0x02,
};

enum class ReadValueTransitionConfiguration {
  CHARACTERISTIC_IID = 0x01,
};

enum class UpdateValueTransitionConfigurationsTypes {
  VALUE_TRANSITION_CONFIGURATION = 0x01,
};

enum class ValueTransitionConfigurationTypes {
  CHARACTERISTIC_IID = 0x01,
  TRANSITION_PARAMETERS = 0x02,
  UNKNOWN_3 = 0x03,  // sent with value = 1 (1 byte)
  UNKNOWN_4 = 0x04,  // not sent yet by anyone
  TRANSITION_CURVE_CONFIGURATION = 0x05,
  UPDATE_INTERVAL = 0x06,            // 16 bit uint
  UNKNOWN_7 = 0x07,                  // not sent yet by anyone
  NOTIFY_INTERVAL_THRESHOLD = 0x08,  // 32 bit uint
};

enum class ValueTransitionParametersTypes {
  TRANSITION_ID = 0x01,  // 16 bytes
  START_TIME = 0x02,     // 8 bytes the start time for the provided schedule,
                         // millis since 2001/01/01 00:00:000
  ID_3 = 0x03,           // 8 bytes, id or something (same for multiple writes)
};

enum class TransitionCurveConfigurationTypes {
  TRANSITION_ENTRY = 0x01,
  ADJUSTMENT_CHARACTERISTIC_IID = 0x02,
  ADJUSTMENT_MULTIPLIER_RANGE = 0x03,
};

enum class TransitionEntryTypes {
  ADJUSTMENT_FACTOR = 0x01,
  VALUE = 0x02,
  OFFSET = 0x03,    // the time in milliseconds from the previous
                    // transition, interpolation happens here
  DURATION = 0x04,  // optional, default 0, sets how long the previous value
                    // will stay the same (non interpolation time section)
};

enum class TransitionAdjustmentMultiplierRange {
  MINIMUM_ADJUSTMENT_MULTIPLIER = 0x01,  // brightness 10
  MAXIMUM_ADJUSTMENT_MULTIPLIER = 0x02,  // brightness 100
};

enum class ValueTransitionConfigurationResponseTypes {  // read format for
                                                        // control point
  VALUE_CONFIGURATION_STATUS = 0x01,
};

enum class
    ValueTransitionConfigurationStatusTypes {  // note, this could be a
                                               // mirror of
                                               // ValueTransitionConfigurationTypes
                                               // when parameter 0x3
                                               // would not be bigger suddenly
                                               // than 1 byte received?
      CHARACTERISTIC_IID = 0x01,
      TRANSITION_PARAMETERS = 0x02,
      TIME_SINCE_START = 0x03,  // milliseconds since start of transition
    };

// FIXME: could be moved to homekit-adk
#define kHAPCharacteristicDebugDescription_CharacteristicValueTransitionControl \
  "transition-control"
const HAPUUID kHAPCharacteristicType_CharacteristicValueTransitionControl =
    HAPUUIDCreateAppleDefined(0x143);

#define kHAPCharacteristicDebugDescription_SupportedCharacteristicValueTransitionConfiguration \
  "transition-configuration"
const HAPUUID
    kHAPCharacteristicType_SupportedCharacteristicValueTransitionConfiguration =
        HAPUUIDCreateAppleDefined(0x144);

#define kHAPCharacteristicDebugDescription_CharacteristicValueActiveTransitionCount \
  "transition-count"
const HAPUUID kHAPCharacteristicType_CharacteristicValueActiveTransitionCount =
    HAPUUIDCreateAppleDefined(0x24B);

namespace shelly {
namespace hap {

template <class T>
const T clamp(const T &v, const T &lo, const T &hi) {
  return std::min(std::max(v, lo), hi);
}

template <typename T>
bool isValid(T *unused HAP_UNUSED) {
  return true;
}

HAP_DATA_TLV_SUPPORT(uuidType, uuidFormatType)

const uuidFormatType uuidFormat = {
    .type = kHAPTLVFormatType_Data,
    .constraints = {.minLength = 16, .maxLength = 16}};

const HAPUInt64TLVFormat uint64Format = {
    .type = kHAPTLVFormatType_UInt64,
    .constraints = {.minimumValue = 0, .maximumValue = UINT64_MAX},
    .callbacks = {.getDescription = NULL, .getBitDescription = NULL}};

const HAPUInt32TLVFormat uint32Format = {
    .type = kHAPTLVFormatType_UInt32,
    .constraints = {.minimumValue = 0, .maximumValue = UINT32_MAX},
    .callbacks = {.getDescription = NULL, .getBitDescription = NULL}};

const HAPUInt8TLVFormat uint8Format = {
    .type = kHAPTLVFormatType_UInt8,
    .constraints = {.minimumValue = 0, .maximumValue = UINT8_MAX},
    .callbacks = {.getDescription = NULL, .getBitDescription = NULL}};

const HAPUInt16TLVFormat uint16Format = {
    .type = kHAPTLVFormatType_UInt16,
    .constraints = {.minimumValue = 0, .maximumValue = UINT16_MAX},
    .callbacks = {.getDescription = NULL, .getBitDescription = NULL}};

const HAPUInt16TLVFormat iidFormat = uint16Format;

const HAPStructTLVMember transitionEntryAdjustmentFactorMember = {
    .valueOffset = HAP_OFFSETOF(transitionEntryType, adjustmentFactor),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType) TransitionEntryTypes::ADJUSTMENT_FACTOR,
    .debugDescription = "ADJUSTMENT_FACTOR",
    .format = &uint32Format,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember transitionEntryValueMember = {
    .valueOffset = HAP_OFFSETOF(transitionEntryType, value),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType) TransitionEntryTypes::VALUE,
    .debugDescription = "VALUE",
    .format = &uint32Format,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember transitionEntryOffsetMember = {
    .valueOffset = HAP_OFFSETOF(transitionEntryType, offset),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType) TransitionEntryTypes::OFFSET,
    .debugDescription = "OFFSET",
    .format = &uint32Format,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember transitionEntryDurationMember = {
    .valueOffset = HAP_OFFSETOF(transitionEntryType, duration),
    .isSetOffset = HAP_OFFSETOF(transitionEntryType, durationPresent),
    .tlvType = (HAPTLVType) TransitionEntryTypes::DURATION,
    .debugDescription = "DURATION",
    .format = &uint32Format,
    .isOptional = true,
    .isFlat = false};

HAP_STRUCT_TLV_SUPPORT(transitionEntryType, transitionItemFormat)
const transitionItemFormat transitionEntryStructFormat = {
    .type = kHAPTLVFormatType_Struct,
    .members =
        (const HAPStructTLVMember *const[]){
            &transitionEntryOffsetMember,
            &transitionEntryAdjustmentFactorMember, &transitionEntryValueMember,
            &transitionEntryDurationMember, NULL},
    .callbacks = {.isValid = isValid<transitionEntryType>}};

const HAPUInt8TLVFormat sepFormat = {
    .type = kHAPTLVFormatType_None,
    .constraints = {.minimumValue = 0, .maximumValue = 0},
    .callbacks = {.getDescription = NULL, .getBitDescription = NULL}};

HAP_SEQUENCE_TLV_SUPPORT(transitionCurveType, transitionItemFormat,
                         curveContainerFormat)

const curveContainerFormat supportCurveContainer = {
    .type = kHAPTLVFormatType_Sequence,
    .item =
        {
            .valueOffset = HAP_OFFSETOF(transitionCurveType, _),
            .tlvType = (HAPTLVType)
                TransitionCurveConfigurationTypes::TRANSITION_ENTRY,
            .debugDescription = "TRANSITION_ENTRY",
            .format = &transitionEntryStructFormat,
            .isFlat = false,
        },
    .separator = {
        .tlvType = 0, .debugDescription = "SEPARATOR", .format = &sepFormat}};

const HAPStructTLVMember supportCurveContainerMember = {
    .valueOffset = HAP_OFFSETOF(transitionCurveConfigurationType, curve),
    .isSetOffset = HAP_OFFSETOF(transitionCurveConfigurationType, curvePresent),
    .tlvType = (HAPTLVType) 0,  // DNC initializer
    .debugDescription = "transitionCurveConfigurationMember",
    .format = &supportCurveContainer,
    .isOptional = false,
    .isFlat = true};

const HAPStructTLVMember adjustmentCharacteristicIIDMember = {
    .valueOffset = HAP_OFFSETOF(transitionCurveConfigurationType, iid),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType)
        TransitionCurveConfigurationTypes::ADJUSTMENT_CHARACTERISTIC_IID,

    .debugDescription = "ADJUSTMENT_CHARACTERISTIC_IID",
    .format = &iidFormat,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember minimumAdjustmentMultiplierMember = {
    .valueOffset = HAP_OFFSETOF(adjustmentMultiplierRangeType,
                                minimumAdjustmentMultiplier),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType)
        TransitionAdjustmentMultiplierRange::MINIMUM_ADJUSTMENT_MULTIPLIER,

    .debugDescription = "MINIMUM_ADJUSTMENT_MULTIPLIER",
    .format = &uint32Format,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember maximumAdjustmentMultiplierMember = {
    .valueOffset = HAP_OFFSETOF(adjustmentMultiplierRangeType,
                                maximumAdjustmentMultiplier),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType)
        TransitionAdjustmentMultiplierRange::MAXIMUM_ADJUSTMENT_MULTIPLIER,

    .debugDescription = "MAXIMUM_ADJUSTMENT_MULTIPLIER",
    .format = &uint32Format,
    .isOptional = false,
    .isFlat = false};

HAP_STRUCT_TLV_SUPPORT(adjustmentMultiplierRangeType,
                       adjustmentMultiplierRangeSType)
const adjustmentMultiplierRangeSType adjustmentMultiplierRangeFormat = {
    .type = kHAPTLVFormatType_Struct,
    .members =
        (const HAPStructTLVMember *const[]){&maximumAdjustmentMultiplierMember,
                                            &minimumAdjustmentMultiplierMember,
                                            NULL},
    .callbacks = {.isValid = isValid<adjustmentMultiplierRangeType>}};

const HAPStructTLVMember adjustmentMultiplierRangeMember = {
    .valueOffset = HAP_OFFSETOF(transitionCurveConfigurationType,
                                adjustmentMultiplierRange),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType)
        TransitionCurveConfigurationTypes::ADJUSTMENT_MULTIPLIER_RANGE,

    .debugDescription = "ADJUSTMENT_MULTIPLIER_RANGE",
    .format = &adjustmentMultiplierRangeFormat,
    .isOptional = false,
    .isFlat = false};

HAP_STRUCT_TLV_SUPPORT(transitionCurveConfigurationType, valueFormatType2)
const valueFormatType2 supportCurveContainerStruct = {
    .type = kHAPTLVFormatType_Struct,
    .members =
        (const HAPStructTLVMember *const[]){&adjustmentCharacteristicIIDMember,
                                            &adjustmentMultiplierRangeMember,
                                            &supportCurveContainerMember, NULL},
    .callbacks = {.isValid = isValid<transitionCurveConfigurationType>}};

const HAPStructTLVMember transitionCurveConfigurationMember = {
    .valueOffset = HAP_OFFSETOF(transitionType, transitionCurveConfiguration),
    .isSetOffset =
        HAP_OFFSETOF(transitionType, transitionCurveConfigurationPresent),
    .tlvType = (HAPTLVType)
        ValueTransitionConfigurationTypes::TRANSITION_CURVE_CONFIGURATION,
    .debugDescription = "TRANSITION_CURVE_CONFIGURATION",
    .format = &supportCurveContainerStruct,
    .isOptional = true,
    .isFlat = false};

const HAPStructTLVMember transitionIdMember = {
    .valueOffset = HAP_OFFSETOF(parametersType, transitionId),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType) ValueTransitionParametersTypes::TRANSITION_ID,
    .debugDescription = "TRANSITION_ID",
    .format = &uuidFormat,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember startTimeMember = {
    .valueOffset = HAP_OFFSETOF(parametersType, startTime),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType) ValueTransitionParametersTypes::START_TIME,
    .debugDescription = "START_TIME",
    .format = &uint64Format,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember id3Member = {
    .valueOffset = HAP_OFFSETOF(parametersType, id3),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType) ValueTransitionParametersTypes::ID_3,
    .debugDescription = "ID_3",
    .format = &uint64Format,
    .isOptional = false,
    .isFlat = false};

HAP_STRUCT_TLV_SUPPORT(parametersType, transitionParametersFormatType)
const transitionParametersFormatType transitionParametersFormat = {
    .type = kHAPTLVFormatType_Struct,
    .members =
        (const HAPStructTLVMember *const[]){&transitionIdMember,
                                            &startTimeMember, &id3Member, NULL},
    .callbacks = {.isValid = isValid<parametersType>}};

const HAPStructTLVMember characteristicIIDMember = {
    .valueOffset = HAP_OFFSETOF(transitionType, iid),
    .isSetOffset = 0,
    .tlvType =
        (HAPTLVType) ValueTransitionConfigurationTypes::CHARACTERISTIC_IID,
    .debugDescription = "CHARACTERISTIC_IID",
    .format = &iidFormat,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember transitionParametersMember = {
    .valueOffset = HAP_OFFSETOF(transitionType, parameters),
    .isSetOffset = HAP_OFFSETOF(transitionType, parametersPresent),
    .tlvType =
        (HAPTLVType) ValueTransitionConfigurationTypes::TRANSITION_PARAMETERS,
    .debugDescription = "TRANSITION_PARAMETERS",
    .format = &transitionParametersFormat,
    .isOptional = true,
    .isFlat = false};

const HAPStructTLVMember unknown3Member = {
    .valueOffset = HAP_OFFSETOF(transitionType, unknown_3),
    .isSetOffset = HAP_OFFSETOF(transitionType, unknown_3Present),
    .tlvType = (HAPTLVType) ValueTransitionConfigurationTypes::UNKNOWN_3,
    .debugDescription = "UNKNOWN_3",
    .format = &uint8Format,
    .isOptional = true,
    .isFlat = false};

const HAPStructTLVMember unknown4Member = {
    .valueOffset = HAP_OFFSETOF(transitionType, unknown_4),
    .isSetOffset = HAP_OFFSETOF(transitionType, unknown_4Present),
    .tlvType = (HAPTLVType) ValueTransitionConfigurationTypes::UNKNOWN_4,
    .debugDescription = "UNKNOWN_4",
    .format = &uint8Format,
    .isOptional = true,
    .isFlat = false};

const HAPStructTLVMember updateIntervalMember = {
    .valueOffset = HAP_OFFSETOF(transitionType, updateInterval),
    .isSetOffset = HAP_OFFSETOF(transitionType, updateIntervalPresent),
    .tlvType = (HAPTLVType) ValueTransitionConfigurationTypes::UPDATE_INTERVAL,
    .debugDescription = "UPDATE_INTERVAL",
    .format = &uint16Format,
    .isOptional = true,
    .isFlat = false};

const HAPStructTLVMember unknown7Member = {
    .valueOffset = HAP_OFFSETOF(transitionType, unknown_7),
    .isSetOffset = HAP_OFFSETOF(transitionType, unknown_7Present),
    .tlvType = (HAPTLVType) ValueTransitionConfigurationTypes::UNKNOWN_7,
    .debugDescription = "UNKNOWN_7",
    .format = &uint16Format,
    .isOptional = true,
    .isFlat = false};

const HAPStructTLVMember notifiyIntervalThresholdMember = {
    .valueOffset = HAP_OFFSETOF(transitionType, notifyIntervalThreshold),
    .isSetOffset = HAP_OFFSETOF(transitionType, notifyIntervalThresholdPresent),
    .tlvType = (HAPTLVType)
        ValueTransitionConfigurationTypes::NOTIFY_INTERVAL_THRESHOLD,
    .debugDescription = "NOTIFY_INTERVAL_THRESHOLD",
    .format = &uint32Format,
    .isOptional = true,
    .isFlat = false};

HAP_STRUCT_TLV_SUPPORT(transitionType, valueFormatType)
const valueFormatType valueTypeFormat = {
    .type = kHAPTLVFormatType_Struct,
    .members =
        (const HAPStructTLVMember *const[]){
            &characteristicIIDMember, &unknown3Member,
            &transitionParametersMember, &unknown4Member,
            &transitionCurveConfigurationMember, &updateIntervalMember,
            &unknown7Member, &notifiyIntervalThresholdMember, NULL},
    .callbacks = {.isValid = isValid<transitionType>}};

HAP_SEQUENCE_TLV_SUPPORT(valueList, valueFormatType, curveContainerFormat2)

const curveContainerFormat2 updateValueTransitionFormat = {
    .type = kHAPTLVFormatType_Sequence,
    .item =
        {
            .valueOffset = HAP_OFFSETOF(valueList, _),
            .tlvType = (HAPTLVType) UpdateValueTransitionConfigurationsTypes::
                VALUE_TRANSITION_CONFIGURATION,
            .debugDescription = "VALUE_TRANSITION_CONFIGURATION",
            .format = &valueTypeFormat,
            .isFlat = false,
        },
    .separator = {
        .tlvType = 0, .debugDescription = "SEPARATOR", .format = &sepFormat}};

const HAPStructTLVMember readValueIIDMember = {
    .valueOffset = HAP_OFFSETOF(supportedConfig, iid),
    .isSetOffset = 0,
    .tlvType =
        (HAPTLVType) ReadValueTransitionConfiguration::CHARACTERISTIC_IID,
    .debugDescription = "CHARACTERISTIC_IID",
    .format = &iidFormat,
    .isOptional = false,
    .isFlat = false};

HAP_STRUCT_TLV_SUPPORT(readTransitionType, readValueTransitionFormatType)
const readValueTransitionFormatType readValueTransitionFormat = {
    .type = kHAPTLVFormatType_Struct,
    .members = (const HAPStructTLVMember *const[]){&readValueIIDMember, NULL},
    .callbacks = {.isValid = isValid<readTransitionType>}};

const HAPStructTLVMember readTransitionMember = {
    .valueOffset = HAP_OFFSETOF(transitionControlTypeRequest, readTransition),
    .isSetOffset =
        HAP_OFFSETOF(transitionControlTypeRequest, readTransitionPresent),
    .tlvType = (HAPTLVType)
        TransitionControlTypes::READ_CURRENT_VALUE_TRANSITION_CONFIGURATION,
    .debugDescription = "READ_CURRENT_VALUE_TRANSITION_CONFIGURATION",
    .format = &readValueTransitionFormat,
    .isOptional = true,
    .isFlat = false};

const HAPStructTLVMember updateTransitionMember = {
    .valueOffset = HAP_OFFSETOF(transitionControlTypeRequest, updateTransition),
    .isSetOffset =
        HAP_OFFSETOF(transitionControlTypeRequest, updateTransitionPresent),
    .tlvType = (HAPTLVType)
        TransitionControlTypes::UPDATE_VALUE_TRANSITION_CONFIGURATION,
    .debugDescription = "UPDATE_VALUE_TRANSITION_CONFIGURATION",
    .format = &updateValueTransitionFormat,
    .isOptional = true,
    .isFlat = false};

const HAPStructTLVMember readTransitionResponseMember = {
    .valueOffset =
        HAP_OFFSETOF(transitionControlTypeResponse, readTransitionResponse),
    .isSetOffset = HAP_OFFSETOF(transitionControlTypeResponse,
                                readTransitionResponsePresent),
    .tlvType = (HAPTLVType)
        TransitionControlTypes::READ_CURRENT_VALUE_TRANSITION_CONFIGURATION,
    .debugDescription = "READ_CURRENT_VALUE_TRANSITION_CONFIGURATION_RESP",
    .format = &updateValueTransitionFormat,  // response contains complete list
    .isOptional = true,
    .isFlat = false};

HAP_STRUCT_TLV_SUPPORT(transitionControlTypeRequest, transitionControlFormat)

const transitionControlFormat transitionControlFormatType = {
    .type = kHAPTLVFormatType_Struct,
    .members =
        (const HAPStructTLVMember *const[]){&readTransitionMember,
                                            &updateTransitionMember, NULL},
    .callbacks = {.isValid = isValid<transitionControlTypeRequest>}};

HAP_STRUCT_TLV_SUPPORT(supportedConfig, supportedConfigFormat)

const HAPStructTLVMember supportedConfigIIDMember = {
    .valueOffset = HAP_OFFSETOF(supportedConfig, iid),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType)
        SupportedValueTransitionConfigurationTypes::CHARACTERISTIC_IID,
    .debugDescription = "CHARACTERISTIC_IID",
    .format = &iidFormat,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember supportedConfigTypeMember = {
    .valueOffset = HAP_OFFSETOF(supportedConfig, type),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType)
        SupportedValueTransitionConfigurationTypes::TRANSITION_TYPE,
    .debugDescription = "TRANSITION_TYPE",
    .format = &uint8Format,
    .isOptional = false,
    .isFlat = false};

const supportedConfigFormat supportedConfigFormatType = {
    .type = kHAPTLVFormatType_Struct,
    .members =
        (const HAPStructTLVMember *const[]){&supportedConfigIIDMember,
                                            &supportedConfigTypeMember, NULL},
    .callbacks = {.isValid = isValid<supportedConfig>}};

HAP_SEQUENCE_TLV_SUPPORT(supportedConfigList, supportedConfigFormat,
                         containerFormat)

const containerFormat supportedTransitionContainer = {
    .type = kHAPTLVFormatType_Sequence,
    .item = {.valueOffset = HAP_OFFSETOF(supportedConfigList, _),
             .tlvType = (HAPTLVType)
                 SupportedCharacteristicValueTransitionConfigurationsTypes::
                     SUPPORTED_TRANSITION_CONFIGURATION,
             .debugDescription = "SUPPORTED_TRANSITION_CONFIGURATION",
             .format = &supportedConfigFormatType,
             .isFlat = false

    },
    .separator = {
        .tlvType = 0, .debugDescription = "SEPARATOR", .format = &sepFormat}};

const HAPStructTLVMember statusResponseTimeMember = {
    .valueOffset = HAP_OFFSETOF(configurationStatus, timeSinceStart),
    .isSetOffset = 0,
    .tlvType =
        (HAPTLVType) ValueTransitionConfigurationStatusTypes::TIME_SINCE_START,
    .debugDescription = "TIME_SINCE_START",
    .format = &uint32Format,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember statusResponseTransitionMember = {
    .valueOffset = HAP_OFFSETOF(configurationStatus, parameters),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType)
        ValueTransitionConfigurationStatusTypes::TRANSITION_PARAMETERS,
    .debugDescription = "TRANSITION_PARAMETERS",
    .format = &transitionParametersFormat,
    .isOptional = false,
    .isFlat = false};

const HAPStructTLVMember statusResponseIIDMember = {
    .valueOffset = HAP_OFFSETOF(configurationStatus, iid),
    .isSetOffset = 0,
    .tlvType = (HAPTLVType)
        ValueTransitionConfigurationStatusTypes::CHARACTERISTIC_IID,
    .debugDescription = "CHARACTERISTIC_IID",
    .format = &iidFormat,
    .isOptional = false,
    .isFlat = false};

HAP_STRUCT_TLV_SUPPORT(configurationStatus, updateTransitionResponseItemFormat)

const updateTransitionResponseItemFormat valueConfigurationStatusFormat = {
    .type = kHAPTLVFormatType_Struct,
    .members =
        (const HAPStructTLVMember *const[]){&statusResponseIIDMember,
                                            &statusResponseTransitionMember,
                                            &statusResponseTimeMember, NULL},
    .callbacks = {.isValid = isValid}};

HAP_SEQUENCE_TLV_SUPPORT(updateTransitionResponseList,
                         updateTransitionResponseItemFormat,
                         updateTransitionResponseFormat)

const updateTransitionResponseFormat valueConfigurationStatusContainer = {
    .type = kHAPTLVFormatType_Sequence,
    .item = {.valueOffset = HAP_OFFSETOF(updateTransitionResponseList, _),
             .tlvType = (HAPTLVType) ValueTransitionConfigurationResponseTypes::
                 VALUE_CONFIGURATION_STATUS,
             .debugDescription = "VALUE_CONFIGURATION_STATUS",
             .format = &valueConfigurationStatusFormat,
             .isFlat = false

    },
    .separator = {
        .tlvType = 0, .debugDescription = "SEPARATOR", .format = &sepFormat}};

const HAPStructTLVMember updateTransitionReponseMember = {
    .valueOffset =
        HAP_OFFSETOF(transitionControlTypeResponse, updateTransitionResponse),
    .isSetOffset = HAP_OFFSETOF(transitionControlTypeResponse,
                                updateTransitionResponsePresent),
    .tlvType = (HAPTLVType)
        TransitionControlTypes::UPDATE_VALUE_TRANSITION_CONFIGURATION,
    .debugDescription = "UPDATE_VALUE_TRANSITION_CONFIGURATION_RESP",
    .format = &valueConfigurationStatusContainer,
    .isOptional = true,
    .isFlat = false};

HAP_STRUCT_TLV_SUPPORT(transitionControlTypeResponse,
                       transitionControlResponseFormat)

const transitionControlResponseFormat transitionControlFormatResponseType = {
    .type = kHAPTLVFormatType_Struct,
    .members =
        (const HAPStructTLVMember *const[]){&readTransitionResponseMember,
                                            &updateTransitionReponseMember,
                                            NULL},
    .callbacks = {.isValid = isValid<transitionControlTypeResponse>}};

template <typename T>
HAPError enumerate_vec(HAPSequenceTLVDataSourceRef *dataSource,
                       HAPSequenceTLVEnumerateCallback callback,
                       void *_Nullable context) {
  T *arr;
  HAPRawBufferCopyBytes(&arr, dataSource, sizeof(arr));
  HAPError err = kHAPError_None;

  bool shouldContinue = true;
  for (const auto &val : *arr) {
    callback(context, (HAPTLVValue *) &val, &shouldContinue);
    if (!shouldContinue) {
      break;
    }
  }
  return err;
}

template <typename TVec, typename TVal>
void VecToSequence(TVal *val, const TVec *vec) {
  val->enumerate = &enumerate_vec<TVec>;
  HAPRawBufferCopyBytes(&val->dataSource, &vec, sizeof(vec));
}

AdaptiveLighting::AdaptiveLighting(LightBulb *bulb, struct mgos_config_lb *cfg)
    : bulb_(bulb),
      cfg_(cfg),
      active_transition_count_(0),
      update_timer_(std::bind(&AdaptiveLighting::UpdateCB, this)) {
  // TODO:restore transition schedule from mgos config / transition_schedule
  // caveat: we cannot know how long we were offline for, nor do we know the
  // current time, so this seems pointless for now
}

AdaptiveLighting::~AdaptiveLighting() {
}

void AdaptiveLighting::Disable() {
  update_timer_.Clear();
  active_transition_count_ = 0;
  transition_count_characteristic_->RaiseEvent();
}

void AdaptiveLighting::ColorTempChangedManually() {
  Disable();
}

void AdaptiveLighting::BrightnessChangedManually() {
  AdjustColorTemp(0);
}

void AdaptiveLighting::UpdateCB() {
  AdjustColorTemp(active_transition_.updateInterval);
}

void AdaptiveLighting::AdjustColorTemp(uint16_t elapsed_time) {
  if (active_transition_count_ != 1) {
    return;
  }
  // eventually we should save the current offset_millis_ so we could
  // continue the transition where we left off (except the unknown downtime)
  // once we also store/restore the transition table from nvmem

  offset_millis_ += elapsed_time;
  notification_millis_ += elapsed_time;

  uint32_t offset_next = 0, offset_curr = 0;

  transitionEntryType curr, next;
  curr = active_table_[0];
  next = active_table_[0];

  // loop over everything until now: could maybe be done more elegantly
  // this is only done every 30 minutes
  for (auto val = active_table_.cbegin(); val != active_table_.cend(); ++val) {
    offset_next += val->offset;

    if (val->durationPresent) {
      offset_next += val->duration;
    }

    next = *val;

    if (offset_millis_ <= offset_next) {
      break;
    }
    curr = *val;
    offset_curr = offset_next;
  }

  if (offset_millis_ > offset_next) {
    Disable();
  }

  float duration = offset_next - offset_curr;
  duration = clamp<float>(duration, 1, INT32_MAX);

  float elapsed = offset_millis_ - offset_curr;
  ;
  float percentage = elapsed / duration;

  if (curr.durationPresent) {
    if (curr.duration > elapsed) {
      percentage = 0;
    } else {
      elapsed -= curr.duration;
      duration -= curr.duration;
      percentage = elapsed / duration;
    }
  }

  percentage = clamp<float>(percentage, 0, 1);

  float val_interp = curr.value + (next.value - curr.value) * percentage;
  float adj_interp =
      curr.adjustmentFactor +
      (next.adjustmentFactor - curr.adjustmentFactor) * percentage;

  auto range = &active_transition_.transitionCurveConfiguration
                    .adjustmentMultiplierRange;

  // could also be another adjustment iid in future
  int32_t adjustmentMultiplier = clamp<int32_t>(
      (int32_t) cfg_->brightness, range->minimumAdjustmentMultiplier,
      range->maximumAdjustmentMultiplier);

  int temperature = val_interp + adj_interp * adjustmentMultiplier;
  LOG(LL_INFO, ("adaptive light: %i mired, elapsed in schedule: %f min",
                temperature, offset_millis_ / 1000 / 60.0));

  std::string changereason = kChangeReasonAuto;
  if (elapsed_time != 0 &&
      notification_millis_ >= active_transition_.notifyIntervalThreshold) {
    changereason = kChangeReasonAutoWithNotification;
    notification_millis_ = 0;
  }

  // could also be another value iid in future
  // temperature by HAP is sometime beyond bounds. HAP
  // Characeristic does not allow this
  temperature = clamp(temperature, 50, 400);
  bulb_->SetColorTemperature(temperature, changereason);
}

Status AdaptiveLighting::Init() {
  if (bulb_->GetBrightnessCharacteristic() == nullptr) {
    LOG(LL_INFO,
        ("Adaptive Lighting not supported, no Brightness Characteristic"));
    return Status::UNIMPLEMENTED();
  }
  if (bulb_->GetColorTemperaturCharacteristic() == nullptr) {
    LOG(LL_INFO, ("Adaptive Lighting not supported, no ColorTemperature "
                  "Characteristic"));
    return Status::UNIMPLEMENTED();
  }

  uint16_t iid = SHELLY_HAP_IID_BASE_ADAPTIVE_LIGHTING;

  transition_configuration_characteristic_ = new mgos::hap::TLV8Characteristic(
      iid++,
      &kHAPCharacteristicType_SupportedCharacteristicValueTransitionConfiguration,
      [this](HAPAccessoryServerRef *server UNUSED_ARG,
             const HAPTLV8CharacteristicReadRequest *request UNUSED_ARG,
             HAPTLVWriterRef *responseWriter,
             void *_Nullable context UNUSED_ARG) {
        uint16_t iidBrightness =
            ((HAPBaseCharacteristic *) bulb_->GetBrightnessCharacteristic()
                 ->GetHAPCharacteristic())
                ->iid;
        uint16_t iidColorTemperature =
            ((HAPBaseCharacteristic *) bulb_->GetColorTemperaturCharacteristic()
                 ->GetHAPCharacteristic())
                ->iid;

        const std::vector<supportedConfig> vec = {
            {.iid = iidColorTemperature,
             .type = (uint8_t) TransitionType::COLOR_TEMPERATURE},
            {.iid = iidBrightness,
             .type = (uint8_t) TransitionType::BRIGHTNESS}};

        supportedConfigList val;
        VecToSequence(&val, &vec);

        return HAPTLVWriterEncode(responseWriter, &supportedTransitionContainer,
                                  &val);
      },
      false /* supports notification */, nullptr, false /* write response */,
      false /* control point */,
      kHAPCharacteristicDebugDescription_SupportedCharacteristicValueTransitionConfiguration);
  bulb_->AddChar(transition_configuration_characteristic_);

  transition_control_characteristic_ = new mgos::hap::TLV8Characteristic(
      iid++, &kHAPCharacteristicType_CharacteristicValueTransitionControl,
      [this](HAPAccessoryServerRef *server UNUSED_ARG,
             const HAPTLV8CharacteristicReadRequest *request UNUSED_ARG,
             HAPTLVWriterRef *responseWriter UNUSED_ARG,
             void *_Nullable context UNUSED_ARG) {
        HAPError err = kHAPError_None;

        std::vector<configurationStatus> vec;

        if (active_transition_count_ == 1) {
          vec.push_back({
              .parameters =
                  {.startTime = active_transition_.parameters.startTime,
                   .id3 = active_transition_.parameters.id3,
                   .transitionId = {.bytes = active_transition_.parameters
                                                 .transitionId.bytes,
                                    .numBytes = active_transition_.parameters
                                                    .transitionId.numBytes}},
              .timeSinceStart = offset_millis_,
              .iid = active_transition_.iid,
          });
        }

        updateTransitionResponseList val = {};
        VecToSequence(&val, &vec);

        if (direct_answer_read_ || direct_answer_update_) {
          LOG(LL_INFO, ("write_response: read %i, update %i",
                        direct_answer_read_, direct_answer_update_));

          transitionControlTypeResponse response = {};

          response.readTransitionResponsePresent = direct_answer_read_;
          response.updateTransitionResponsePresent = direct_answer_update_;

          if (direct_answer_update_) {
            direct_answer_update_ = false;

            if (active_transition_count_ == 1) {
              response.updateTransitionResponse = val;
            } else {
              response.updateTransitionResponsePresent = false;
            }
          }

          updateTransitionType update;
          valueList list = {};
          std::vector<transitionType> vec;

          if (direct_answer_read_) {
            direct_answer_read_ = false;
            vec.push_back(active_transition_);
          }
          VecToSequence(&list, &vec);

          update.value = list;
          response.readTransitionResponse = update;

          err = HAPTLVWriterEncode(
              responseWriter, &transitionControlFormatResponseType, &response);

        } else {
          LOG(LL_INFO, ("control point: direct read"));
          err = HAPTLVWriterEncode(responseWriter,
                                   &valueConfigurationStatusContainer, &val);
        }
        return err;
      },
      false,
      [this](HAPAccessoryServerRef *server UNUSED_ARG,
             const HAPTLV8CharacteristicWriteRequest *request UNUSED_ARG,
             HAPTLVReaderRef *responseReader,
             void *_Nullable context UNUSED_ARG) {
        HAPPrecondition(responseReader);

        HAPError err = kHAPError_None;

        transitionControlTypeRequest transitionRequest = {};

        const HAPTLVReader *reader = (const HAPTLVReader *) responseReader;

        LOG(LL_INFO, ("control point: write %zu bytes", reader->numBytes));

        err = HAPTLVReaderDecode(responseReader, &transitionControlFormatType,
                                 &transitionRequest);
        if (err != kHAPError_None) {
          LOG(LL_ERROR, ("Error occured while decoding request"));
          return err;
        }

        if (transitionRequest.readTransitionPresent) {
          direct_answer_read_ = true;

          // specific iids will be needed once we would support more than one
          // transition
        }

        transitionTypeIterationContext transition_context = {
            .type = &active_transition_, .count = 0};
        {
          valueList *curve = &transitionRequest.updateTransition.value;

          err = curve->enumerate(
              &curve->dataSource,
              [](void *_Nullable context, HAPTLVValue *value,
                 bool *shouldContinue) {
                transitionTypeIterationContext *enc =
                    (transitionTypeIterationContext *) context;

                transitionType *v = (transitionType *) value;

                if (enc->count >= 1) {
                  *shouldContinue = false;
                  LOG(LL_ERROR, ("transitions are more than supported (1)"));
                } else {
                  *enc->type = *v;
                  enc->count++;
                }
              },
              &transition_context);

          if (err != kHAPError_None) {
            return err;
          }
        }

        if (transition_context.count >= 1 &&
            active_transition_.transitionCurveConfigurationPresent) {
          direct_answer_update_ = true;
          transitionCurveType *curve =
              &active_transition_.transitionCurveConfiguration.curve;

          active_table_.clear();
          tableIterationContext table_context = {.vec = &active_table_,
                                                 .count = 0};

          err = curve->enumerate(
              &curve->dataSource,
              [](void *_Nullable context, HAPTLVValue *value,
                 bool HAP_UNUSED *shouldContinue) {
                tableIterationContext *enc = (tableIterationContext *) context;

                transitionEntryType *v = (transitionEntryType *) value;

                enc->vec->push_back(*v);
              },
              &table_context);

          LOG(LL_INFO, ("Received table with size: %zu", active_table_.size()));

          if (err != kHAPError_None) {
            return err;
          }

          // encode active_table back for encoding
          transitionCurveType *table =
              &active_transition_.transitionCurveConfiguration.curve;

          VecToSequence(table, &active_table_);

          // deep copy of parameters.uuid
          active_transition_.parameters.transitionId.numBytes = 16;
          memcpy(&active_transition_id_,
                 active_transition_.parameters.transitionId.bytes,
                 active_transition_.parameters.transitionId.numBytes);

          active_transition_.parameters.transitionId.bytes =
              &active_transition_id_;

          uint16_t iidColorTemperature =
              ((HAPBaseCharacteristic *) bulb_
                   ->GetColorTemperaturCharacteristic()
                   ->GetHAPCharacteristic())
                  ->iid;

          if (active_transition_.iid != iidColorTemperature) {
            LOG(LL_ERROR, ("Error occured while decoding request"));
            return kHAPError_InvalidState;
          }

          if (!active_transition_.unknown_3Present) {
            LOG(LL_INFO, ("Schedule deactivated"));
            Disable();
          } else {
            LOG(LL_INFO, ("Schedule activated"));
            // TODO: store configuration as base64 encoded val
            // e.g mgos_conf_set_str(&cfg_->transition_schedule, encodedval);
            // this would use ~1 kB of storage, but only if we have a notion of
            // time

            active_transition_count_ = 1;
            transition_count_characteristic_->RaiseEvent();
            offset_millis_ = 0;
            notification_millis_ = 0;

            update_timer_.Reset(active_transition_.updateInterval,
                                MGOS_TIMER_REPEAT | MGOS_TIMER_RUN_NOW);
          }
        } else {
          active_transition_count_ = 0;
        }

        return kHAPError_None;
      },
      true /* write response */, true /* control point */,
      kHAPCharacteristicDebugDescription_CharacteristicValueTransitionControl);
  bulb_->AddChar(transition_control_characteristic_);

  transition_count_characteristic_ = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_CharacteristicValueActiveTransitionCount,
      0, 255, 1,
      std::bind(&mgos::hap::ReadUInt8<uint8_t>, _1, _2, _3,
                &active_transition_count_),
      true /* supports_notification */, nullptr,
      kHAPCharacteristicDebugDescription_CharacteristicValueActiveTransitionCount);
  bulb_->AddChar(transition_count_characteristic_);

  return Status::OK();
}

}  // namespace hap
}  // namespace shelly
