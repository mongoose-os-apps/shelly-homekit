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

#include "shelly_common.hpp"

#include "mgos_timers.hpp"

namespace shelly {

class Output {
 public:
  explicit Output(int id);
  virtual ~Output();

  int id() const;
  virtual bool GetState() = 0;
  virtual Status SetState(bool on, const char *source) = 0;
  virtual Status SetStatePWM(float duty, const char *source) = 0;
  virtual Status Pulse(bool on, int duration_ms, const char *source) = 0;
  virtual void SetInvert(bool out_invert) = 0;

 private:
  const int id_;
  Output(const Output &other) = delete;
};

class OutputPin : public Output {
 public:
  OutputPin(int id, int pin, int on_value);
  virtual ~OutputPin();

  // Output interface impl.
  bool GetState() override;
  Status SetState(bool on, const char *source) override;
  Status SetStatePWM(float duty, const char *source) override;
  Status Pulse(bool on, int duration_ms, const char *source) override;
  int pin() const;
  void SetInvert(bool out_invert) override;

 protected:
  bool out_invert_ = false;

 private:
  void PulseTimerCB();

  const int pin_;
  const int on_value_;

  bool pulse_active_ = false;
  mgos::Timer pulse_timer_;

  OutputPin(const OutputPin &other) = delete;
};

}  // namespace shelly
