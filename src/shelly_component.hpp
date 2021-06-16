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

namespace shelly {

class Component {
 public:
  enum class Type {
    kSwitch = 0,
    kOutlet = 1,
    kLock = 2,
    kStatelessSwitch = 3,
    kWindowCovering = 4,
    kGarageDoorOpener = 5,
    kDisabledInput = 6,
    kMotionSensor = 7,
    kOccupancySensor = 8,
    kContactSensor = 9,
    kDoorbell = 10,
    kLightBulb = 11,
    kMax,
  };

  explicit Component(int id);
  virtual ~Component();

  int id() const;

  // Complex initialization after construction.
  virtual Status Init() = 0;
  virtual Type type() const = 0;
  virtual std::string name() const = 0;

  // Short status snippet string.
  virtual StatusOr<std::string> GetInfo() const = 0;
  // Full JSON status for UI.
  virtual StatusOr<std::string> GetInfoJSON() const = 0;
  // Set configuration from UI.
  virtual Status SetConfig(const std::string &config_json,
                           bool *restart_required) = 0;
  // Set state from UI.
  virtual Status SetState(const std::string &state_json) = 0;

  // Is there any activity going on?
  // If true is returned, it means it's ok to destroy the component.
  // False should be returned if there is any user-visible activity,
  // like curtain moving.
  // Default implementation always returns true.
  virtual bool IsIdle();

 private:
  const int id_;

  Component(const Component &other) = delete;
};

}  // namespace shelly
