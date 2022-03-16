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

#include "shelly_input.hpp"

namespace shelly {

// Reset device WiFi and auth settings.
void ResetDevice(int out_gpio);

// Wipe all device configuration, including WiFi.
bool WipeDevice();

// Wipe device before reverting to stock FW.
// Preserves WiFi STA config.
void WipeDeviceRevertToStock();

// Handler for input reset sequence.
void HandleInputResetSequence(Input *in, int out_gpio, Input::Event ev,
                              bool cur_state);

bool IsSoftReboot();

bool IsFailsafeMode();

void SanitizeSysConfig();

// Check if device is in a reboot loop (deliberate or not) and perform reset.
void CheckRebootCounter();

}  // namespace shelly
