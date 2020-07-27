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

#include "shelly_debug.h"

#include "mgos.h"
#include "mgos_http_server.h"

#include "HAPAccessoryServer+Internal.h"
#include "HAPPlatformTCPStreamManager+Init.h"

static HAPPlatformKeyValueStoreRef s_kvs;
static HAPPlatformTCPStreamManagerRef s_tcpm;

void shelly_debug_write_nc(struct mg_connection *nc) {
  uint16_t cn;
  if (HAPAccessoryServerGetCN(s_kvs, &cn) != kHAPError_None) {
    cn = 0;
  }
  HAPPlatformTCPStreamManagerStats tcpm_stats = {};
  HAPPlatformTCPStreamManagerGetStats(s_tcpm, &tcpm_stats);
  mg_printf(nc,
            "Uptime: %.2lf\r\n"
            "RAM: %lu free, %lu min free\r\n"
            "HAP config number: %u\r\n"
            "HAP connection stats: %u/%u/%u\r\n",
            mgos_uptime(), (unsigned long) mgos_get_free_heap_size(),
            (unsigned long) mgos_get_min_free_heap_size(), cn,
            (unsigned) tcpm_stats.numPendingTCPStreams,
            (unsigned) tcpm_stats.numActiveTCPStreams,
            (unsigned) tcpm_stats.maxNumTCPStreams);
  mg_printf(nc, "HAP connections:\r\n");
  time_t now_wall = mg_time();
  int64_t now_micros = mgos_uptime_micros();
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
    mg_printf(nc, "  %p %s f %#lx io %d ts %p rd %lld\r\n", nc2, addr,
              (unsigned long) nc2->flags, last_io_age, ts,
              (long long) (last_read_age / 1000000));
    num_hap_connections++;
  }
  mg_printf(nc, " Total: %d", num_hap_connections);
}

void shelly_get_debug_info(std::string *out) {
  struct mg_connection nc = {};
  shelly_debug_write_nc(&nc);
  *out = std::string(nc.send_mbuf.buf, nc.send_mbuf.len);
  mbuf_free(&nc.send_mbuf);
}

static void shelly_debug_handler(struct mg_connection *nc, int ev,
                                 void *ev_data, void *user_data) {
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

bool shelly_debug_init(HAPPlatformKeyValueStoreRef kvs,
                       HAPPlatformTCPStreamManagerRef tcpm) {
  s_kvs = kvs;
  s_tcpm = tcpm;
  mgos_register_http_endpoint("/debug/", shelly_debug_handler, NULL);
  return true;
}
