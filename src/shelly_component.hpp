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
  };

  explicit Component(int id);
  virtual ~Component();

  int id() const;

  virtual Type type() const = 0;
  virtual Status Init() = 0;
  virtual StatusOr<std::string> GetInfo() const = 0;
  virtual Status SetConfig(const std::string &config_json,
                           bool *restart_required) = 0;

 private:
  const int id_;

  Component(const Component &other) = delete;
};

}  // namespace shelly
