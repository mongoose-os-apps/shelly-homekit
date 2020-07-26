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

#include "mgos_http_server.h"

#include "HAPAccessoryServer+Internal.h"

static HAPPlatformKeyValueStoreRef s_kvs;

static void shelly_debug_handler(struct mg_connection *nc, int ev,
                                 void *ev_data, void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  uint16_t cn;
  if (HAPAccessoryServerGetCN(s_kvs, &cn) != kHAPError_None) {
    cn = 0;
  }
  mg_send_response_line(nc, 200,
                        "Content-Type: text/html\r\n"
                        "Connection: close\r\n");
  mg_printf(nc, "<pre>\r\n");
  mg_printf(nc, "Config number: %u\r\n", cn);
  mg_printf(nc, "HAP connections:\r\n");
  time_t now = mg_time();
  int num_hap_connections = 0;
  struct mg_connection *nc2 = NULL;
  for (nc2 = mg_next(nc->mgr, NULL); nc2 != NULL; nc2 = mg_next(nc->mgr, nc2)) {
    if (nc2->listener == NULL ||
        ntohs(nc2->listener->sa.sin.sin_port) != 9000) {
      continue;
    }
    char addr[32];
    mg_sock_addr_to_str(&nc2->sa, addr, sizeof(addr),
                        MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
    mg_printf(nc, "  %s last_io %d\r\n", addr, (int) (now - nc2->last_io_time));
    num_hap_connections++;
  }
  mg_printf(nc, " Total: %d", num_hap_connections);

  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) ev_data;
  (void) user_data;
}

bool shelly_debug_init(HAPPlatformKeyValueStoreRef kvs) {
  s_kvs = kvs;
  mgos_register_http_endpoint("/debug/", shelly_debug_handler, NULL);
  return true;
}
