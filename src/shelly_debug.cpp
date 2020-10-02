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

#include "shelly_debug.hpp"

#include "mgos.hpp"
#include "mgos_core_dump.h"
#include "mgos_file_logger.h"
#include "mgos_http_server.h"
#include "mgos_vfs.h"
#ifdef MGOS_HAVE_OTA_COMMON
#include "esp_coredump.h"
#include "esp_rboot.h"
#include "mgos_ota.h"
#include "mgos_vfs_dev_part.h"
#endif

#include "HAPAccessoryServer+Internal.h"
#include "HAPPlatformTCPStreamManager+Init.h"

#include "shelly_main.hpp"

namespace shelly {

static HAPAccessoryServerRef s_svr;
static HAPPlatformKeyValueStoreRef s_kvs;
static HAPPlatformTCPStreamManagerRef s_tcpm;

struct EnumHAPSessionsContext {
  struct mg_connection *nc;
  int num_sessions;
};

static void EnumHAPSessions(void *vctx, HAPAccessoryServerRef *svr_,
                            HAPSessionRef *s, bool *) {
  EnumHAPSessionsContext *ctx = (EnumHAPSessionsContext *) vctx;
  size_t si = HAPAccessoryServerGetIPSessionIndex(svr_, s);
  const HAPAccessoryServer *svr = (const HAPAccessoryServer *) svr_;
  const HAPIPSession *is = &svr->ip.storage->sessions[si];
  const auto *sd = (const HAPIPSessionDescriptor *) &is->descriptor;
  mg_printf(ctx->nc, "  %d: s %p ts %p o %d st %d ts %lu\r\n", (int) si, s,
            (void *) sd->tcpStream, sd->tcpStreamIsOpen, sd->state,
            (unsigned long) sd->stamp);
  ctx->num_sessions++;
}

void shelly_debug_write_nc(struct mg_connection *nc) {
  uint16_t cn;
  if (HAPAccessoryServerGetCN(s_kvs, &cn) != kHAPError_None) {
    cn = 0;
  }
  HAPPlatformTCPStreamManagerStats tcpm_stats = {};
  HAPPlatformTCPStreamManagerGetStats(s_tcpm, &tcpm_stats);
  mg_printf(nc,
            "App: %s %s %s\r\n"
            "Uptime: %.2lf\r\n"
            "RAM: %lu free, %lu min free\r\n"
            "HAP server port: %d\r\n"
            "HAP config number: %u\r\n"
            "HAP connection stats: %u/%u/%u\r\n",
            MGOS_APP, mgos_sys_ro_vars_get_fw_version(),
            mgos_sys_ro_vars_get_fw_id(), mgos_uptime(),
            (unsigned long) mgos_get_free_heap_size(),
            (unsigned long) mgos_get_min_free_heap_size(),
            HAPPlatformTCPStreamManagerGetListenerPort(s_tcpm), cn,
            (unsigned) tcpm_stats.numPendingTCPStreams,
            (unsigned) tcpm_stats.numActiveTCPStreams,
            (unsigned) tcpm_stats.maxNumTCPStreams);
  mg_printf(nc, "HAP connections:\r\n");
  time_t now_wall = mg_time();
  int64_t now_micros = mgos_uptime_micros();
  {
    int num_hap_connections = 0;
    struct mg_connection *nc2 = NULL;
    struct mg_mgr *mgr = mgos_get_mgr();
    for (nc2 = mg_next(mgr, NULL); nc2 != NULL; nc2 = mg_next(mgr, nc2)) {
      if (nc2->listener == NULL ||
          ntohs(nc2->listener->sa.sin.sin_port) != 9000) {
        continue;
      }
      char addr[32];
      mg_sock_addr_to_str(&nc2->sa, addr, sizeof(addr),
                          MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      int last_io_age = (int) (now_wall - nc2->last_io_time);
      int64_t last_read_age = 0;
      HAPPlatformTCPStream *ts = (HAPPlatformTCPStream *) nc2->user_data;
      if (ts != nullptr) {
        last_read_age = now_micros - ts->lastRead;
      }
      mg_printf(nc, "  %s nc %pf %#lx io %d ts %p rd %lld\r\n", addr, nc2,
                (unsigned long) nc2->flags, last_io_age, ts,
                (long long) (last_read_age / 1000000));
      num_hap_connections++;
    }
    mg_printf(nc, " Total: %d\r\n", num_hap_connections);
  }
  {
    mg_printf(nc, "HAP sessions:\r\n");
    EnumHAPSessionsContext ctx = {.nc = nc, .num_sessions = 0};
    HAPAccessoryServerEnumerateConnectedSessions(&s_svr, EnumHAPSessions, &ctx);
    mg_printf(nc, " Total: %d\r\n", ctx.num_sessions);
  }
}

void GetDebugInfo(std::string *out) {
  struct mg_connection nc = {};
  shelly_debug_write_nc(&nc);
  *out = std::string(nc.send_mbuf.buf, nc.send_mbuf.len);
  mbuf_free(&nc.send_mbuf);
}

void SetDebugEnable(bool debug_en) {
  mgos_sys_config_set_file_logger_enable(debug_en);
  mgos::ScopedCPtr fn(mgos_file_log_get_cur_file_name());
  if (!debug_en) {
    mgos_file_log_flush();
    if (fn.get() != nullptr) {
      remove((const char *) fn.get());
    }
  }
}

static void DebugInfoHandler(struct mg_connection *nc, int ev, void *ev_data,
                             void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  mg_send_response_line(nc, 200,
                        "Content-Type: text/html\r\n"
                        "Connection: close\r\n");
  mg_printf(nc, "<pre>\r\n");
  shelly_debug_write_nc(nc);
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) ev_data;
  (void) user_data;
}

static void DebugLogHandler(struct mg_connection *nc, int ev, void *ev_data,
                            void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  struct http_message *hm = (struct http_message *) ev_data;
  mgos::ScopedCPtr fn(mgos_file_log_get_cur_file_name());
  if (fn.get() == nullptr) {
    mg_http_send_error(nc, 404, "No log file");
    return;
  }
  mgos_file_log_flush();
  mg_http_serve_file(nc, hm, (char *) fn.get(), mg_mk_str("text/plain"),
                     MG_NULL_STR);
  (void) user_data;
}

#ifdef MGOS_HAVE_OTA_COMMON
struct CoreHandlerCtx {
  struct mgos_vfs_dev *dev;
  size_t offset;
};

#define CORE_CHUNK_SIZE 200

static void DebugCoreHandler(struct mg_connection *nc, int ev, void *ev_data,
                             void *user_data) {
  char buf[CORE_CHUNK_SIZE];
  CoreHandlerCtx *ctx = static_cast<CoreHandlerCtx *>(user_data);
  switch (ev) {
    case MG_EV_HTTP_REQUEST: {
      auto *dev = mgos_vfs_dev_open("core");
      if (dev == nullptr) {
        struct mgos_ota_status ota_status = {};
        mgos_ota_get_status(&ota_status);
        rboot_config bcfg = rboot_get_config();
        int cd_slot = (ota_status.partition == 0 ? 1 : 0);
        unsigned long cd_addr = bcfg.roms[cd_slot];
        unsigned long cd_size = bcfg.roms_sizes[cd_slot];
        const std::string s = mgos::JSONPrintStringf(
            "{dev: %Q, offset: %lu, size: %lu}", "sfl0", cd_addr, cd_size);
        dev = mgos_vfs_dev_create(MGOS_VFS_DEV_TYPE_PART, s.c_str());
        if (dev == nullptr) {
          mg_http_send_error(nc, 500, "Failed to open core device");
          return;
        }
      }
      if (mgos_vfs_dev_read(dev, 0, CORE_CHUNK_SIZE, buf) !=
          MGOS_VFS_DEV_ERR_NONE) {
        mg_http_send_error(nc, 500, "Device read failed");
        nc->flags |= MG_F_SEND_AND_CLOSE;
        return;
      }
      if (mg_strstr(mg_mk_str_n(buf, CORE_CHUNK_SIZE),
                    mg_mk_str(MGOS_CORE_DUMP_BEGIN)) == nullptr) {
        mg_http_send_error(nc, 404, "No core dump");
        nc->flags |= MG_F_SEND_AND_CLOSE;
        return;
      }
      mg_send_response_line(nc, 200, "Content-Type: text/plain");
      mg_printf(nc, "Transfer-Encoding: chunked\r\n\r\n");
      mg_send_http_chunk(nc, buf, CORE_CHUNK_SIZE);
      ctx = static_cast<CoreHandlerCtx *>(calloc(1, sizeof(*ctx)));
      if (ctx == nullptr) {
        nc->flags |= MG_F_SEND_AND_CLOSE;
        return;
      }
      ctx->dev = dev;
      ctx->offset = CORE_CHUNK_SIZE;
      nc->user_data = ctx;
      // Take over the connection.
      nc->proto_handler = nullptr;
      nc->handler = DebugCoreHandler;
      break;
    }
    case MG_EV_SEND: {
      if (ctx == nullptr) break;
      size_t nread = (mgos_vfs_dev_get_size(ctx->dev) - ctx->offset);
      if (nread > CORE_CHUNK_SIZE) {
        nread = CORE_CHUNK_SIZE;
      }
      bool last = (nread != CORE_CHUNK_SIZE);
      if (nread > 0) {
        if (mgos_vfs_dev_read(ctx->dev, ctx->offset, nread, buf) !=
            MGOS_VFS_DEV_ERR_NONE) {
          LOG(LL_ERROR, ("Error reading"));
          nc->flags |= MG_F_SEND_AND_CLOSE;
          break;
        }
      }
      struct mg_str s = mg_mk_str_n(buf, nread);
      if (mg_str_starts_with(s, mg_mk_str(MGOS_CORE_DUMP_END))) {
        s.len = strlen(MGOS_CORE_DUMP_END);
        last = true;
      } else {
        const char *np = mg_strchr(s, '\n');
        if (np != nullptr) {
          s.len = (np - s.p + 1);
        }
      }
      mg_send_http_chunk(nc, buf, s.len);
      ctx->offset += s.len;
      if (last) {
        LOG(LL_INFO, ("LAST %d %d", (int) ctx->offset, (int) s.len));
        mg_send_http_chunk(nc, NULL, 0);
        nc->flags |= MG_F_SEND_AND_CLOSE;
      }
      break;
    }
    case MG_EV_CLOSE: {
      if (ctx == nullptr) break;
      mgos_vfs_dev_close(ctx->dev);
      free(ctx);
      nc->user_data = nullptr;
      break;
    }
  }
  (void) ev_data;
  (void) user_data;
}
#endif

bool DebugInit(HAPAccessoryServerRef svr, HAPPlatformKeyValueStoreRef kvs,
               HAPPlatformTCPStreamManagerRef tcpm) {
  s_svr = svr;
  s_kvs = kvs;
  s_tcpm = tcpm;
  mgos_register_http_endpoint("/debug/info", DebugInfoHandler, NULL);
  mgos_register_http_endpoint("/debug/log", DebugLogHandler, nullptr);
#ifdef MGOS_HAVE_OTA_COMMON
  mgos_register_http_endpoint("/debug/core", DebugCoreHandler, nullptr);
#endif
  return true;
}

}  // namespace shelly
