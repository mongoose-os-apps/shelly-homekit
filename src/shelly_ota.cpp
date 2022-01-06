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

#include "shelly_ota.hpp"

#include "mgos.hpp"
#include "mgos_ota.h"

#if CS_PLATFORM == CS_P_ESP8266
#include "esp_coredump.h"
#endif

#include "shelly_main.hpp"

namespace shelly {

static HAPAccessoryServerRef *s_server;
std::unique_ptr<OTAProgress> s_ota_progress;

OTAProgress::OTAProgress(const std::string &version, const std::string &build)
    : version(version), build(build), progress_pct(0) {
}

static void OTABeginCB(int ev UNUSED_ARG, void *ev_data,
                       void *userdata UNUSED_ARG) {
  static double s_wait_start = 0;
  struct mgos_ota_begin_arg *arg = (struct mgos_ota_begin_arg *) ev_data;
  // Some other callback objected.
  if (arg->result != MGOS_UPD_OK) return;
  // If there is some ongoing activity, wait for it to finish.
  if (!AllComponentsIdle()) {
    arg->result = MGOS_UPD_WAIT;
    return;
  }
  // Check app name.
  if (mg_vcmp(&arg->mi.name, MGOS_APP) != 0) {
    LOG(LL_ERROR,
        ("Wrong app name '%.*s'", (int) arg->mi.name.len, arg->mi.name.p));
    arg->result = MGOS_UPD_ABORT;
    return;
  }
  // Stop the HAP server.
  if (!(GetServiceFlags() & SHELLY_SERVICE_FLAG_UPDATE)) {
    s_wait_start = mgos_uptime();
  }
  SetServiceFlags(SHELLY_SERVICE_FLAG_UPDATE);
  ClearServiceFlags(SHELLY_SERVICE_FLAG_REVERT);
  // Are we reverting to stock? Stock fw does not have "hk_model"
  char *hk_model = nullptr;
  json_scanf(arg->mi.manifest.p, arg->mi.manifest.len, "{shelly_hk_model: %Q}",
             &hk_model);
  mgos::ScopedCPtr hk_model_owner(hk_model);
  if (hk_model == nullptr) {
    // hk_model was added in 2.9.1, for now we also check version in the
    // manifest to double-check: stock has manifest version always set to "1.0"
    // while HK has actual version there.
    // This check can be removed after a few versions with hk_model.
    if (mg_vcmp(&arg->mi.version, "1.0") == 0) {
      LOG(LL_INFO, ("This is a revert to stock firmware"));
      SetServiceFlags(SHELLY_SERVICE_FLAG_REVERT);
    }
  }
  if (HAPAccessoryServerGetState(s_server) != kHAPAccessoryServerState_Idle) {
    // There is a bug in HAP server where it will get stuck and fail to shut
    // down reported to happen after approximately 25 days. This is a workaround
    // until it's fixed.
    if (mgos_uptime() - s_wait_start > 10) {
      LOG(LL_WARN,
          ("Server failed to stop, proceeding with the update anyway"));
    } else {
      arg->result = MGOS_UPD_WAIT;
      StopService();
      return;
    }
  }
  LOG(LL_INFO, ("Starting firmware update"));
  s_ota_progress.reset(new OTAProgress(mgos::ToString(arg->mi.version),
                                       mgos::ToString(arg->mi.build_id)));
}

static void WipeDeviceRevertToStockCB(void *) {
  WipeDeviceRevertToStock();
}

static void OTAStatusCB(int ev, void *ev_data, void *userdata) {
  struct mgos_ota_status *arg = (struct mgos_ota_status *) ev_data;
  // Restart server in case of error.
  // In case of success we are going to reboot anyway.
  if (arg->state == MGOS_OTA_STATE_PROGRESS) {
    s_ota_progress->progress_pct = arg->progress_percent;
  } else if (arg->state == MGOS_OTA_STATE_ERROR) {
    s_ota_progress.reset();
    ClearServiceFlags(SHELLY_SERVICE_FLAG_UPDATE | SHELLY_SERVICE_FLAG_REVERT);
  } else if (arg->state == MGOS_OTA_STATE_SUCCESS) {
    s_ota_progress->progress_pct = 100;
#if CS_PLATFORM == CS_P_ESP8266
    // Disable flash core dump because it would overwite the new fw.
    esp_core_dump_set_flash_area(0, 0);
#endif
    if (GetServiceFlags() & SHELLY_SERVICE_FLAG_REVERT) {
      // For some reason if WipeDeviceRevertToStock is done inline the client
      // doesn't get a response to the POST request.
      mgos_set_timer(100, 0, WipeDeviceRevertToStockCB, nullptr);
    }
  }
  (void) ev;
  (void) ev_data;
  (void) userdata;
}

StatusOr<OTAProgress> GetOTAProgress() {
  if (!s_ota_progress) {
    return mgos::Errorf(STATUS_FAILED_PRECONDITION, "No update in progress");
  }
  return *s_ota_progress;
}

void OTAInit(HAPAccessoryServerRef *server) {
  s_server = server;
  mgos_event_add_handler(MGOS_EVENT_OTA_BEGIN, OTABeginCB, nullptr);
  mgos_event_add_handler(MGOS_EVENT_OTA_STATUS, OTAStatusCB, nullptr);
}

}  // namespace shelly
