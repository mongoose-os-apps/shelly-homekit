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

Status BL0942PowerMeter::Init() {
  if (rx_pin_ < 0 && tx_pin_ < 0) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "no valid pins");
  }

  struct mgos_uart_config ucfg;

  ucfg.baud_rate = 4800;

  ucfg.dev.rx_gpio = rx_pin_;
  ucfg.dev.tx_gpio = tx_pin_;

  if (!mgos_uart_configure(uart_no_, &ucfg)) {
    LOG(LL_ERROR, ("Failed to configure UART%d", uart_no_));
  }

  meas_timer_.Reset(meas_time_ * 1000, MGOS_TIMER_REPEAT);
  LOG(LL_INFO, ("BL0942 @ %d/%d", rx_pin_, tx_pin_));
  return Status::OK();
}

StatusOr<float> BL0942PowerMeter::GetPowerW() {
  return apa_;
}

StatusOr<float> BL0942PowerMeter::GetEnergyWH() {
  return aea_;
}

#define BL_READ 0x58
#define BL_ADDR 0x3

#define BL_WATT 0x6

bool BL0942PowerMeter::ReadReg(uint8_t reg, uint8_t *rx_buf, size_t len) {
  uint8_t tx_buf[2] = {BL_READ | BL_ADDR, reg};
  mgos_uart_write(uart_no_, tx_buf, 2);
  mgos_uart_flush(uart_no_);

  // Delay to allow data to be available
  int baud = 4800;
  mgos_msleep(roundf(len * 8 / baud) * 1e3);

  int read_len = mgos_uart_read(uart_no_, rx_buf, len);

  uint8_t chksum = tx_buf[0] + tx_buf[1];
  for (int i = 0; i < len - 1; i++) {
    chksum += rx_buf[i];
    LOG(LL_ERROR, "%08X", rx_buf[i]);
  }
  chksum ^= 0xFF;

  if (read_len != len || rx_buf[len - 1] != chksum) {
    LOG(LL_ERROR, "wrong checksum");
    return false;
  }
  return true;
}

void BL0942PowerMeter::MeasureTimerCB() {
  int len = 4;  // including 1 checksum byte
  uint8_t rx_buf[len];
  if (this->ReadReg(BL_WATT, rx_buf, len)) {
    uint32_t d = rx_buf[2] << 16 | rx_buf[1] << 8 | rx_buf[0];
    if (d & (1 << 23)) {
      d |= 0xFF000000;
    }
    apa_ = d;
  }
}

}  // namespace shelly
