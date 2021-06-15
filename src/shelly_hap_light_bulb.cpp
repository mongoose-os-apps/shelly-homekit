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

#include "shelly_hap_light_bulb.hpp"
#include "shelly_main.hpp"
#include "shelly_switch.hpp"

#include "mgos.hpp"
#include "mgos_system.hpp"

#include "mgos_hap_accessory.hpp"

namespace shelly {
namespace hap {

LightBulb::LightBulb(int id, Input *in, Output *out_r, Output *out_g,
                     Output *out_b, Output *out_w, struct mgos_config_lb *cfg)
    : Component(id),
      Service((SHELLY_HAP_IID_BASE_LIGHTING +
               (SHELLY_HAP_IID_STEP_LIGHTING * (id - 1))),
              &kHAPServiceType_LightBulb,
              kHAPServiceDebugDescription_LightBulb),
      in_(in),
      out_r_(out_r),
      out_g_(out_g),
      out_b_(out_b),
      out_w_(out_w),
      cfg_(cfg),
      auto_off_timer_(std::bind(&LightBulb::AutoOffTimerCB, this)),
      transition_timer_(std::bind(&LightBulb::TransitionTimerCB, this)) {
}

LightBulb::~LightBulb() {
  if (in_ != nullptr) {
    in_->RemoveHandler(handler_id_);
  }

  SaveState();
}

Component::Type LightBulb::type() const {
  return Type::kLightBulb;
}

std::string LightBulb::name() const {
  return cfg_->name;
}

Status LightBulb::Init() {
  if (!cfg_->enable) {
    LOG(LL_INFO, ("'%s' is disabled", cfg_->name));
    return Status::OK();
  }

  uint16_t iid = svc_.iid + 1;

  // Name
  AddNameChar(iid++, cfg_->name);
  // On
  on_characteristic = new mgos::hap::BoolCharacteristic(
      iid++, &kHAPCharacteristicType_On,
      std::bind(&LightBulb::HandleOnRead, this, _1, _2, _3),
      true /* supports_notification */,
      std::bind(&LightBulb::HandleOnWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_On);
  AddChar(on_characteristic);
  // Brightness
  brightness_characteristic = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_Brightness, 0, 100, 1,
      std::bind(&LightBulb::HandleBrightnessRead, this, _1, _2, _3),
      true /* supports_notification */,
      std::bind(&LightBulb::HandleBrightnessWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_Brightness);
  AddChar(brightness_characteristic);
  // Hue
  hue_characteristic = new mgos::hap::UInt32Characteristic(
      iid++, &kHAPCharacteristicType_Hue, 0, 360, 1,
      std::bind(&LightBulb::HandleHueRead, this, _1, _2, _3),
      true /* supports_notification */,
      std::bind(&LightBulb::HandleHueWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_Hue);
  AddChar(hue_characteristic);
  // Saturation
  saturation_characteristic = new mgos::hap::UInt32Characteristic(
      iid++, &kHAPCharacteristicType_Saturation, 0, 100, 1,
      std::bind(&LightBulb::HandleSaturationRead, this, _1, _2, _3),
      true /* supports_notification */,
      std::bind(&LightBulb::HandleSaturationWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_Saturation);
  AddChar(saturation_characteristic);

  if (in_ != nullptr) {
    handler_id_ =
        in_->AddHandler(std::bind(&LightBulb::InputEventHandler, this, _1, _2));
    in_->SetInvert(cfg_->in_inverted);
  }

  bool should_restore = (cfg_->initial_state == (int) InitialState::kLast);
  if (IsSoftReboot()) should_restore = true;

  if (should_restore) {
    UpdateOnOff(IsOn(), "init", true /* force */);
  } else {
    switch (static_cast<InitialState>(cfg_->initial_state)) {
      case InitialState::kOff:
        UpdateOnOff(false, "init", true /* force */);
        break;
      case InitialState::kOn:
        UpdateOnOff(true, "init", true /* force */);
        break;
      case InitialState::kInput:
        if (in_ != nullptr &&
            cfg_->in_mode == static_cast<int>(InMode::kToggle)) {
          UpdateOnOff(in_->GetState(), "init", true /* force */);
        }
        break;
      case InitialState::kLast:
      case InitialState::kMax:
        break;
    }
  }

  return Status::OK();
}

void LightBulb::HSVtoRGBW(RGBW &rgbw) const {
  float h = cfg_->hue / 360.0f;
  float s = cfg_->saturation / 100.0f;
  float v = cfg_->brightness / 100.0f;

  if (cfg_->saturation == 0) {
    // if saturation is zero than all rgb channels same as brightness
    rgbw.r = rgbw.g = rgbw.b = v;
  } else {
    // otherwise calc rgb from hsv (hue, saturation, brightness)
    int i = static_cast<int>(h * 6);
    float f = (h * 6.0f - i);
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);

    switch (i % 6) {
      case 0:  // 0° ≤ h < 60°
        rgbw.r = v;
        rgbw.g = t;
        rgbw.b = p;
        break;

      case 1:  // 60° ≤ h < 120°
        rgbw.r = q;
        rgbw.g = v;
        rgbw.b = p;
        break;

      case 2:  // 120° ≤ h < 180°
        rgbw.r = p;
        rgbw.g = v;
        rgbw.b = t;
        break;

      case 3:  // 180° ≤ h < 240°
        rgbw.r = p;
        rgbw.g = q;
        rgbw.b = v;
        break;

      case 4:  // 240° ≤ h < 300°
        rgbw.r = t;
        rgbw.g = p;
        rgbw.b = v;
        break;

      case 5:  // 300° ≤ h < 360°
        rgbw.r = v;
        rgbw.g = p;
        rgbw.b = q;
        break;
    }
  }

  if (out_w_ != nullptr) {
    // apply white channel to rgb if available
    rgbw.w = std::min(rgbw.r, std::min(rgbw.g, rgbw.b));
    rgbw.r = rgbw.r - rgbw.w;
    rgbw.g = rgbw.g - rgbw.w;
    rgbw.b = rgbw.b - rgbw.w;
  }
}

void LightBulb::UpdateOnOff(bool on, const std::string &source, bool force) {
  if (!force && cfg_->state == static_cast<int>(on)) return;

  LOG(LL_INFO, ("State changed (%s): %s => %s", source.c_str(),
                OnOff(cfg_->state), OnOff(on)));

  cfg_->state = on;
  dirty_ = true;
  on_characteristic->RaiseEvent();

  if (IsOff()) {
    DisableAutoOff();
  }

  StartTransition();
}

void LightBulb::SetHue(int hue, const std::string &source) {
  if (cfg_->hue == hue) return;

  LOG(LL_INFO, ("Hue changed (%s): %d => %d", source.c_str(), cfg_->hue, hue));

  cfg_->hue = hue;
  dirty_ = true;
  hue_characteristic->RaiseEvent();

  StartTransition();
}

void LightBulb::SetSaturation(int saturation, const std::string &source) {
  if (cfg_->saturation == saturation) return;

  LOG(LL_INFO, ("Saturation changed (%s): %d => %d", source.c_str(),
                cfg_->saturation, saturation));

  cfg_->saturation = saturation;
  dirty_ = true;
  saturation_characteristic->RaiseEvent();

  StartTransition();
}

void LightBulb::SetBrightness(int brightness, const std::string &source) {
  if (cfg_->brightness == brightness) return;

  LOG(LL_INFO, ("Brightness changed (%s): %d => %d", source.c_str(),
                cfg_->brightness, brightness));

  cfg_->brightness = brightness;
  dirty_ = true;
  brightness_characteristic->RaiseEvent();

  StartTransition();
}

void LightBulb::StartTransition() {
  rgbw_start_ = rgbw_now_;

  if (IsOn()) {
    ResetAutoOff();
    HSVtoRGBW(rgbw_end_);
  } else {
    // turn off
    rgbw_end_.r = rgbw_end_.g = rgbw_end_.b = rgbw_end_.w = 0.0f;
  }

  LOG(LL_INFO, ("Transition started... %d [ms]", cfg_->transition_time));

  LOG(LL_INFO, ("Output 1: %.2f => %.2f", rgbw_start_.r, rgbw_end_.r));
  LOG(LL_INFO, ("Output 2: %.2f => %.2f", rgbw_start_.g, rgbw_end_.g));
  LOG(LL_INFO, ("Output 3: %.2f => %.2f", rgbw_start_.b, rgbw_end_.b));
  LOG(LL_INFO, ("Output 4: %.2f => %.2f", rgbw_start_.w, rgbw_end_.w));

  // restarting transition timer to fade
  transition_start_ = mgos_uptime_micros();
  transition_timer_.Reset(10, MGOS_TIMER_REPEAT);
}

StatusOr<std::string> LightBulb::GetInfo() const {
  const_cast<LightBulb *>(this)->SaveState();
  return mgos::SPrintf("sta: %s, b: %i, h: %i, sa: %i", OnOff(IsOn()),
                       cfg_->brightness, cfg_->hue, cfg_->saturation);
}

StatusOr<std::string> LightBulb::GetInfoJSON() const {
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, state: %B, "
      " brightness: %d, hue: %d, saturation: %d, "
      " in_inverted: %B, initial: %d, in_mode: %d, "
      "auto_off: %B, auto_off_delay: %.3f, transition_time: %d}",
      id(), type(), cfg_->name, cfg_->state, cfg_->brightness, cfg_->hue,
      cfg_->saturation, cfg_->in_inverted, cfg_->initial_state, cfg_->in_mode,
      cfg_->auto_off, cfg_->auto_off_delay, cfg_->transition_time);
}

Status LightBulb::SetConfig(const std::string &config_json,
                            bool *restart_required) {
  struct mgos_config_lb cfg = *cfg_;
  int8_t in_inverted = -1;
  cfg.name = nullptr;
  cfg.in_mode = -2;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, in_mode: %d, in_inverted: %B, "
             "initial_state: %d, "
             "auto_off: %B, auto_off_delay: %lf, transition_time: %d}",
             &cfg.name, &cfg.in_mode, &in_inverted, &cfg.initial_state,
             &cfg.auto_off, &cfg.auto_off_delay, &cfg.transition_time);

  mgos::ScopedCPtr name_owner((void *) cfg.name);
  // Validation.
  if (cfg.name != nullptr && strlen(cfg.name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (cfg.in_mode != -2 &&
      (cfg.in_mode < 0 || cfg.in_mode >= (int) InMode::kMax)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "in_mode");
  }
  if (cfg.initial_state < 0 || cfg.initial_state >= (int) InitialState::kMax ||
      (cfg_->in_mode == -1 &&
       cfg.initial_state == (int) InitialState::kInput)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "initial_state");
  }
  cfg.auto_off = (cfg.auto_off != 0);
  if (cfg.initial_state < 0 || cfg.initial_state > (int) InitialState::kMax) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "initial_state");
  }
  // Now copy over.
  if (cfg_->name != nullptr && strcmp(cfg_->name, cfg.name) != 0) {
    mgos_conf_set_str(&cfg_->name, cfg.name);
    *restart_required = true;
  }
  if (cfg.in_mode != -2 && cfg_->in_mode != cfg.in_mode) {
    if (cfg_->in_mode == (int) InMode::kDetached ||
        cfg.in_mode == (int) InMode::kDetached) {
      *restart_required = true;
    }
    cfg_->in_mode = cfg.in_mode;
  }
  if (in_inverted != -1 && cfg_->in_inverted != in_inverted) {
    cfg_->in_inverted = in_inverted;
    *restart_required = true;
  }
  cfg_->initial_state = cfg.initial_state;
  cfg_->auto_off = cfg.auto_off;
  cfg_->auto_off_delay = cfg.auto_off_delay;
  cfg_->transition_time = cfg.transition_time;
  return Status::OK();
}

void LightBulb::SaveState() {
  if (!dirty_) return;
  mgos_sys_config_save(&mgos_sys_config, false /* try_once */, NULL /* msg */);
  dirty_ = false;
}

Status LightBulb::SetState(const std::string &state_json) {
  int8_t state = -1;
  int brightness = -1, hue = -1, saturation = -1;

  json_scanf(state_json.c_str(), state_json.size(),
             "{state: %B, brightness: %d, hue: %d, saturation: %d}", &state,
             &brightness, &hue, &saturation);

  if (hue != -1 && (hue < 0 || hue > 360)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid hue: %d (only 0-360)",
                        hue);
  }

  if (saturation != -1 && (saturation < 0 || saturation > 100)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT,
                        "invalid saturation: %d (only 0-100)", saturation);
  }

  if (brightness != -1 && (brightness < 0 || brightness > 100)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT,
                        "invalid brightness: %d (only 0-100)", brightness);
  }

  if (state != -1) UpdateOnOff(static_cast<bool>(state), "RPC");
  if (hue != -1) SetHue(hue, "RPC");
  if (saturation != -1) SetSaturation(saturation, "RPC");
  if (brightness != -1) SetBrightness(brightness, "RPC");

  return Status::OK();
}

void LightBulb::ResetAutoOff() {
  auto_off_timer_.Reset(cfg_->auto_off_delay * 1000, 0);
}

void LightBulb::DisableAutoOff() {
  auto_off_timer_.Clear();
}

bool LightBulb::IsOn() const {
  return cfg_->state != 0;
}

bool LightBulb::IsOff() const {
  return cfg_->state == 0;
}

bool LightBulb::IsAutoOffEnabled() const {
  return cfg_->auto_off != 0;
}

void LightBulb::AutoOffTimerCB() {
  // Don't set state if auto off has been disabled during timer run
  if (!IsAutoOffEnabled()) return;
  if (static_cast<InMode>(cfg_->in_mode) == InMode::kActivation &&
      in_ != nullptr && in_->GetState() && IsOn()) {
    // Input is active, re-arm.
    LOG(LL_INFO, ("Input is active, re-arming auto off timer"));
    ResetAutoOff();
    return;
  }
  UpdateOnOff(false, "auto_off");
}

void LightBulb::TransitionTimerCB() {
  int64_t elapsed = mgos_uptime_micros() - transition_start_;
  int64_t duration = cfg_->transition_time * 1000;

  if (elapsed > duration) {
    transition_timer_.Clear();
    rgbw_now_ = rgbw_end_;
    LOG(LL_INFO, ("Transition ready"));
  } else {
    float alpha = static_cast<float>(elapsed) / static_cast<float>(duration);
    rgbw_now_.r = alpha * rgbw_end_.r + (1 - alpha) * rgbw_start_.r;
    rgbw_now_.g = alpha * rgbw_end_.g + (1 - alpha) * rgbw_start_.g;
    rgbw_now_.b = alpha * rgbw_end_.b + (1 - alpha) * rgbw_start_.b;
    rgbw_now_.w = alpha * rgbw_end_.w + (1 - alpha) * rgbw_start_.w;
  }

  out_r_->SetStatePWM(rgbw_now_.r, "transition");
  out_g_->SetStatePWM(rgbw_now_.g, "transition");
  out_b_->SetStatePWM(rgbw_now_.b, "transition");

  if (out_w_ != nullptr) {
    out_w_->SetStatePWM(rgbw_now_.w, "transition");
  }
}

void LightBulb::InputEventHandler(Input::Event ev, bool state) {
  InMode in_mode = static_cast<InMode>(cfg_->in_mode);
  if (in_mode == InMode::kDetached) {
    // Nothing to do
    return;
  }
  switch (ev) {
    case Input::Event::kChange: {
      switch (static_cast<InMode>(cfg_->in_mode)) {
        case InMode::kMomentary:
          if (state) {  // Only on 0 -> 1 transitions.
            UpdateOnOff(IsOff(), "ext_mom");
          }
          break;
        case InMode::kToggle:
          UpdateOnOff(state, "switch");
          break;
        case InMode::kEdge:
          UpdateOnOff(IsOff(), "ext_edge");
          break;
        case InMode::kActivation:
          if (state) {
            UpdateOnOff(true, "ext_act");
          } else if (IsOn() && IsAutoOffEnabled()) {
            // On 1 -> 0 transitions do not turn on output
            // but re-arm auto off timer if running.
            ResetAutoOff();
          }
          break;
        case InMode::kAbsent:
        case InMode::kDetached:
        case InMode::kMax:
          break;
      }
      break;
    }
    case Input::Event::kLong:
      // Disable auto-off if it was active.
      if (in_mode == InMode::kMomentary) {
        DisableAutoOff();
      }
      break;
    case Input::Event::kSingle:
    case Input::Event::kDouble:
    case Input::Event::kReset:
    case Input::Event::kMax:
      break;
  }
}

HAPError LightBulb::HandleOnRead(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicReadRequest *request, bool *value) {
  LOG(LL_INFO, ("On read %d: %s", id(), OnOff(value)));
  *value = IsOn();
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError LightBulb::HandleOnWrite(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicWriteRequest *request, bool value) {
  LOG(LL_INFO, ("On write %d: %s", id(), OnOff(value)));
  UpdateOnOff(value, "HAP");
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError LightBulb::HandleBrightnessRead(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicReadRequest *request, uint8_t *value) {
  LOG(LL_INFO, ("Brightness read %d: %d", id(), cfg_->brightness));
  *value = static_cast<uint8_t>(cfg_->brightness);
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError LightBulb::HandleBrightnessWrite(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicWriteRequest *request, uint8_t value) {
  LOG(LL_INFO, ("Brightness write %d: %d", id(), static_cast<int>(value)));
  SetBrightness(value, "HAP");
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError LightBulb::HandleHueRead(
    HAPAccessoryServerRef *server,
    const HAPUInt32CharacteristicReadRequest *request, uint32_t *value) {
  LOG(LL_INFO, ("Hue read %d: %d", id(), cfg_->hue));
  *value = static_cast<uint32_t>(cfg_->hue);
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError LightBulb::HandleHueWrite(
    HAPAccessoryServerRef *server,
    const HAPUInt32CharacteristicWriteRequest *request, uint32_t value) {
  LOG(LL_INFO, ("Hue write %d: %d", id(), static_cast<int>(value)));
  SetHue(value, "HAP");
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError LightBulb::HandleSaturationRead(
    HAPAccessoryServerRef *server,
    const HAPUInt32CharacteristicReadRequest *request, uint32_t *value) {
  LOG(LL_INFO, ("Saturation read %d: %d", id(), cfg_->saturation));
  *value = static_cast<uint32_t>(cfg_->saturation);
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError LightBulb::HandleSaturationWrite(
    HAPAccessoryServerRef *server,
    const HAPUInt32CharacteristicWriteRequest *request, uint32_t value) {
  LOG(LL_INFO, ("Saturation write %d: %d", id(), static_cast<int>(value)));
  SetSaturation(value, "HAP");
  (void) server;
  (void) request;
  return kHAPError_None;
}

}  // namespace hap
}  // namespace shelly
