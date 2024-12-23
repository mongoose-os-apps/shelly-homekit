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

#include "shelly_pm_bl0942.hpp"

#include <cmath>

#include "mgos.hpp"

struct packet {
  uint8_t frame_header;
  uint8_t i_rms[3];
  uint8_t v_rms[3];
  uint8_t i_fast_rms[3];
  uint8_t watt[3];
  uint8_t cf_cnt[3];
  uint8_t frequency[2];
  uint8_t reserved1;
  uint8_t status;
  uint8_t reserved2;
  uint8_t reserved3;
  uint8_t checksum;
} __attribute__((packed));

namespace shelly {

BL0942PowerMeter::BL0942PowerMeter(int id, int tx_pin, int rx_pin,
                                   int meas_time, int uart_no)
    : PowerMeter(id),
      tx_pin_(tx_pin),
      rx_pin_(rx_pin),
      meas_time_(meas_time),
      uart_no_(uart_no),
      meas_timer_(std::bind(&BL0942PowerMeter::MeasureTimerCB, this)) {
}

BL0942PowerMeter::~BL0942PowerMeter() {
}

#define BL_READ 0x58
#define BL_WRITE 0xA8

#define BL_SOFT_RESET 0x1C
#define BL_USR_WRPROT 0x1D
#define BL_MODE 0x19
#define BL_TPS_CTRL 0x1B
#define BL_I_FAST_RMS_CTRL 0x10

#define BL_ADDR 0x0

#define BL_WATT 0x6

Status BL0942PowerMeter::Init() {
  if (rx_pin_ < 0 && tx_pin_ < 0) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "no valid pins");
  }

  struct mgos_uart_config ucfg;
  mgos_uart_config_set_defaults(uart_no_, &ucfg);

  ucfg.baud_rate = 9600;

  ucfg.dev.rx_gpio = rx_pin_;
  ucfg.dev.tx_gpio = tx_pin_;
  ucfg.dev.cts_gpio = -1;
  ucfg.dev.rts_gpio = -1;

  if (!mgos_uart_configure(uart_no_, &ucfg)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "Failed to configure UART");
  }

  mgos_uart_set_rx_enabled(uart_no_, true);

  meas_timer_.Reset(meas_time_ * 1000, MGOS_TIMER_REPEAT);
  LOG(LL_INFO, ("BL0942 @ %d/%d", rx_pin_, tx_pin_));

  this->WriteReg(BL_SOFT_RESET, 0x5a5a5a);
  // this->WriteReg(BL_USR_WRPROT, 0x550000);
  // this->WriteReg(BL_MODE, 0x001000);
  // this->WriteReg(BL_TPS_CTRL, 0xFF4700);
  // this->WriteReg(BL_I_FAST_RMS_CTRL, 0x1C1800);

  return Status::OK();
}

StatusOr<float> BL0942PowerMeter::GetPowerW() {
  return apa_;
}

StatusOr<float> BL0942PowerMeter::GetEnergyWH() {
  return aea_;
}

bool BL0942PowerMeter::WriteReg(uint8_t reg, uint32_t val) {
  uint8_t tx_buf[6] = {BL_WRITE | BL_ADDR,
                       reg,
                       (uint8_t) ((val >> 16) & 0xFF),
                       (uint8_t) ((val >> 8) & 0xFF),
                       (uint8_t) ((val >> 0) & 0xFF),
                       0};

  for (int i = 0; i < 5; i++) {
    tx_buf[5] += tx_buf[i];
  }
  tx_buf[5] = tx_buf[5] ^ 0xFF;
  mgos_uart_write(uart_no_, tx_buf, 6);
  mgos_uart_flush(uart_no_);
  mgos_msleep(1);
  return true;
}

bool BL0942PowerMeter::ReadReg(uint8_t reg, uint8_t *rx_buf, size_t len) {
  bool whole_packet = (len == 23);
  uint8_t tx_buf[2] = {BL_READ | BL_ADDR, reg};
  mgos_uart_write(uart_no_, tx_buf, 2);
  mgos_uart_flush(uart_no_);

  // Delay to allow data to be available
  int baud = 9600;
  mgos_msleep(roundf(len * 8 / baud) * 1e3);

  int read_len = mgos_uart_read(uart_no_, rx_buf, len);

  uint8_t chksum =
      tx_buf[0] +
      (whole_packet ? 0 : tx_buf[1]);  // ignore tx_buf[1] when reading packet
  for (int i = 0; i < len - 1; i++) {
    chksum += rx_buf[i];
    // LOG(LL_INFO, ("data %i:%02X", i, rx_buf[i]));
  }
  chksum ^= 0xFF;

  if (read_len != len || rx_buf[len - 1] != chksum) {
    LOG(LL_ERROR, ("wrong checksum"));
    return false;
  }
  return true;
}

uint32_t convert_le24(uint8_t v[3]) {
  return ((uint32_t) v[2] << 16) | (uint32_t) (v[1] << 8) | v[0];
}

uint32_t convert_le16(uint8_t v[2]) {
  return (uint32_t) (v[1] << 8) | v[0];
}

void BL0942PowerMeter::MeasureTimerCB() {
  packet rx_buf;
  static uint32_t cf_cnt = 0;

  if (this->ReadReg(0xAA, (uint8_t *) &rx_buf, sizeof(rx_buf))) {
    if (rx_buf.frame_header == 0x55) {
      uint32_t cf = convert_le24(rx_buf.cf_cnt);
      cf = (cf_cnt & 0xFF000000) | cf;
      if (cf_cnt > cf) {
        cf += 0x1000000;
      }
      cf_cnt = cf;

      float wref = (3537 / (1.218 * 1.218 * 4));
      float vref = (73989 / (1.218 * 4));
      float iref = (305978 / (1.218));

      float vo = convert_le24(rx_buf.v_rms) / vref;
      float vi = convert_le24(rx_buf.i_rms) / iref;
      int32_t wa_tmp = convert_le24(rx_buf.watt);
      if (wa_tmp & 0x800000) {
        wa_tmp |= 0xFF000000;
      }
      float wa = wa_tmp / wref;
      float fr = 1000000.0 / (float) convert_le16(rx_buf.frequency);

      apa_ = wa;
      aea_ = cf / (wref * 3600 / (1638.4 * 256));

      LOG(LL_INFO, ("vo: %.1f wa: %.2f i: %.2f fr: %.2f ae: %.2f", vo, wa, vi,
                    fr, aea_));
    }
  }
}

}  // namespace shelly
