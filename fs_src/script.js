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

// Options.
const maxAuthAge = 24 * 60 * 60;
const updateCheckInterval = 24 * 60 * 60;

// Globals.
var lastInfo = null;
var infoLevel = 0;
var host = null;
var socket = null;
var connectionTries = 0;

var pauseAutoRefresh = false;
var pendingGetInfo = false;

var pendingRequests = {};  // id -> Promise.

let nextRequestID = Math.ceil(Math.random() * 10000);
let authRequired = false;

const authInfoKey = "auth_info";
const authUser = "admin";
var authRealm = null;
var rpcAuth = null;

function el(container, id) {
  if (id === undefined) {
    id = container;
    container = document;
  }
  return container.querySelector(`#${id}`);
}

function checkName(name) {
  return !!name.match(/^[a-z0-9\-]{1,63}$/i)
}

el("sys_save_btn").onclick = function () {
  if (!checkName(el("sys_name").value)) {
    alert(`Name must be between 1 and 63 characters
           and consist of letters, numbers or dashes ('-')`);
    return;
  }
  var data = {
    config: {
      name: el("sys_name").value,
      sys_mode: parseInt(el("sys_mode").value),
    },
  };
  el("sys_save_spinner").className = "spin";
  pauseAutoRefresh = true;
  callDevice("Shelly.SetConfig", data).then(function () {
    setTimeout(() => {
      el("sys_save_spinner").className = "";
      pauseAutoRefresh = false;
      refreshUI();
    }, 1300);
  }).catch(function (err) {
    el("sys_save_spinner").className = "";
    if (err.response) err = err.response.data.message;
    pauseAutoRefresh = false;
    alert(err);
  });
};

el("hap_setup_btn").onclick = function () {
  el("hap_setup_spinner").className = "spin";
  callDevice("HAP.Setup", {"code": "RANDOMCODE", "id": "RANDOMID"})
    .then(function (resp) {
      var info = resp.result;
      console.log(info);
      if (!info) return;
      el("hap_setup_code").innerText = info.code;
      el("qrcode").innerText = "";
      new QRCode(el("qrcode"), {
        text: info.url,
        width: 160,
        height: 160,
        colorDark : "black",
        colorLight : "white",
        correctLevel: QRCode.CorrectLevel.Q,
      });
      el("hap_setup_info").style.display = "block";
      getInfo();
    })
    .catch(function (err) {
      if (err.response) {
        err = err.response.data.message;
      }
      alert(err);
    }).finally(function () {
      el("hap_setup_spinner").className = "";
    });
};

el("hap_reset_btn").onclick = function () {
  if(!confirm("This will erase all pairings and clear setup code. Are you sure?")) return;

  el("hap_reset_spinner").className = "spin";
  el("hap_setup_info").style.display = "none";
  callDevice("HAP.Reset", {"reset_server": true, "reset_code": true})
    .then(function () {
      el("hap_reset_spinner").className = "";
      getInfo();
    })
    .catch(function (err) {
      if (err.response) {
        err = err.response.data.message;
      }
      alert(err);
    });
};

el("fw_upload_btn").onclick = async function () {
  let ff = el("fw_select_file").files;
  if (ff.length == 0) {
    alert("No files selected");
    return true;
  }
  uploadFW(ff[0], el("fw_spinner"), el("update_status"));
  return true;
};

el("wifi_save_btn").onclick = function () {
  el("wifi_spinner").className = "spin";
  var data = {
    config: {
      wifi: {
        sta: {enable: el("wifi_en").checked, ssid: el("wifi_ssid").value},
        ap: {enable: !el("wifi_en").checked},
      },
    },
    reboot: true,
  };
  if (el("wifi_pass").value && el("wifi_pass").value.length >= 8) {
    data.config.wifi.sta.pass = el("wifi_pass").value;
  }
  var oldPauseAutoRefresh = pauseAutoRefresh;
  pauseAutoRefresh = true;
  callDevice("Config.Set", data).then(function () {
    var dn = el("device_name").innerText;
    if (data.config.wifi.sta.enable) {
      document.body.innerHTML = `
        <div class='container'><h1>Rebooting...</h1>
          <p>Device is rebooting and connecting to <b>${el("wifi_ssid").value}</b>.</p>
          <p>
            Connect to the same network and visit
            <a href='http://${dn}.local/'>http://${dn}.local/</a>.
          </p>
          <p>
            If device cannot be contacted, see
            <a href='https://github.com/mongoose-os-apps/shelly-homekit/wiki/Recovery'>here</a> for recovery options.
          </p>
        </div>."`;
    } else {
      document.body.innerHTML = `
        <div class='container'><h1>Rebooting...</h1>
          <p>Device is rebooting into AP mode.</p>
          <p>
            It can be reached by connecting to <b>${lastInfo.wifi_ap_ssid}</b>
            and navigating to <a href='http://${lastInfo.wifi_ap_ip}/'>http://${lastInfo.wifi_ap_ip}/</a>.
          </p>
          <p>
            If device cannot be contacted, see
            <a href='https://github.com/mongoose-os-apps/shelly-homekit/wiki/Recovery'>here</a> for recovery options.
          </p>
        </div>."`;
    }
  }).catch(function (err) {
    el("wifi_spinner").className = "";
    pauseAutoRefresh = oldPauseAutoRefresh;
    if (err.response) {
      err = err.response.data.message;
    }
    alert(err);
  });
};

function setComponentConfig(c, cfg, spinner) {
  if (spinner) spinner.className = "spin";
  var data = {
    id: c.data.id,
    type: c.data.type,
    config: cfg,
  };
  pauseAutoRefresh = true;
  callDevice("Shelly.SetConfig", data)
    .then(function () {
      setTimeout(() => {
        if (spinner) spinner.className = "";
        pauseAutoRefresh = false;
        refreshUI();
      }, 1300);
    }).catch(function (err) {
    if (spinner) spinner.className = "";
    if (err.response) {
      err = err.response.data.message;
    }
    alert(err);
    pauseAutoRefresh = false;
  });
}

function setComponentState(c, state, spinner) {
  if (spinner) spinner.className = "spin";
  var data = {
    id: c.data.id,
    type: c.data.type,
    state: state,
  };
  callDevice("Shelly.SetState", data)
    .then(function () {
      if (spinner) spinner.className = "";
      refreshUI();
    }).catch(function (err) {
    if (spinner) spinner.className = "";
    if (err.response) {
      err = err.response.data.message;
    }
    alert(err);
  });
}

function autoOffDelayValid(value) {
  parsedValue = dateStringToSeconds(value);
  return (parsedValue >= 0.010) && (parsedValue <= 2147483.647);
}

function dateStringToSeconds(dateString) {
  if (dateString == "") return 0;

  var {
    days, hours, minutes, seconds, minutes, milliseconds
  } = dateString.match(
    /^(?<days>\d+)\:(?<hours>\d{2})\:(?<minutes>\d{2})\:(?<seconds>\d{2})\.(?<milliseconds>\d{3})/
  ).groups

  var seconds = parseInt(days) * 24 * 3600 +
    parseInt(hours) * 3600 +
    parseInt(minutes) * 60 +
    parseInt(seconds) +
    parseFloat(milliseconds / 1000);
  return seconds;
}

function secondsToDateString(seconds) {
  if (seconds == 0) return "";
  var date = new Date(1970, 0, 1);
  date.setMilliseconds(seconds * 1000);
  var dateString = Math.floor(seconds / 3600 / 24) + ":" +
    nDigitString(date.getHours(), 2) + ":" +
    nDigitString(date.getMinutes(), 2) + ":" +
    nDigitString(date.getSeconds(), 2) + "." +
    nDigitString(date.getMilliseconds(), 3);
  return dateString;
}

function nDigitString(num, digits) {
  return num.toString().padStart(digits, "0");
}

function rgbSetConfig(c) {
  var name = el(c, "name").value;
  var initialState = el(c, "initial").value;
  var autoOff = el(c, "auto_off").checked;
  var autoOffDelay = el(c, "auto_off_delay").value;
  var spinner = el(c, "save_spinner");

  if (name == "") {
    alert("Name must not be empty");
    return;
  }

  if (autoOff && autoOffDelay && !autoOffDelayValid(autoOffDelay)) {
    alert("Auto off delay must follow 24 hour format D:HH:MM:SS.sss with a value between 10ms and 24 days.");
    return;
  }

  var cfg = {
    name: name,
    initial_state: parseInt(el(c, "initial").value),
    auto_off: autoOff,
    in_inverted: el(c, "in_inverted").checked,
    transition_time: parseInt(el(c, "transition_time").value)
  };
  if (autoOff) {
    cfg.auto_off_delay = dateStringToSeconds(autoOffDelay);
  }
  if (c.data.in_mode >= 0) {
    cfg.in_mode = parseInt(el(c, "in_mode").value);
  }
  setComponentConfig(c, cfg, spinner);
}

function swSetConfig(c) {
  var name = el(c, "name").value;
  var svcType = el(c, "svc_type").value;
  var charType = el(c, "valve_type").value;
  var initialState = el(c, "initial").value;
  var autoOff = el(c, "auto_off").checked;
  var autoOffDelay = el(c, "auto_off_delay").value;
  var spinner = el(c, "save_spinner");

  if (name == "") {
    alert("Name must not be empty");
    return;
  }

  if (autoOff && autoOffDelay && !autoOffDelayValid(autoOffDelay)) {
    alert("Auto off delay must follow 24 hour format D:HH:MM:SS.sss with a value between 10ms and 24 days.");
    return;
  }

  var cfg = {
    name: name,
    svc_type: parseInt(el(c, "svc_type").value),
    initial_state: parseInt(el(c, "initial").value),
    auto_off: autoOff,
    in_inverted: el(c, "in_inverted").checked,
    out_inverted: el(c, "out_inverted").checked,
  };
  if (autoOff) {
    cfg.auto_off_delay = dateStringToSeconds(autoOffDelay);
  }
  if (c.data.state_led_en >= 0) {
    cfg.state_led_en = el(c, "state_led_en").checked ? 1 : 0;
  }
  if (c.data.in_mode >= 0) {
    cfg.in_mode = parseInt(el(c, "in_mode").value);
  }
  cfg.valve_type = (svcType == 3) ? parseInt(el(c, "valve_type").value) : -1;
  setComponentConfig(c, cfg, spinner);
}

function sswSetConfig(c) {
  var name = el(c, "name").value;
  if (name == "") {
    alert("Name must not be empty");
    return;
  }
  var cfg = {
    name: name,
    type: parseInt(el(c, "type").value),
    inverted: el(c, "inverted").checked,
    in_mode: parseInt(el(c, "in_mode").value),
  };
  setComponentConfig(c, cfg, el(c, "save_spinner"));
}

function diSetConfig(c) {
  var cfg = {
    type: parseInt(el(c, "type").value),
  };
  setComponentConfig(c, cfg, el(c, "save_spinner"));
}

function mosSetConfig(c) {
  var name = el(c, "name").value;
  if (name == "") {
    alert("Name must not be empty");
    return;
  }
  var cfg = {
    name: name,
    type: parseInt(el(c, "type").value),
    inverted: el(c, "inverted").checked,
    in_mode: parseInt(el(c, "in_mode").value),
    idle_time: parseInt(el(c, "idle_time").value),
  };
  setComponentConfig(c, cfg, el(c, "save_spinner"));
}

function wcSetConfig(c, cfg, spinner) {
  if (!cfg) {
    var name = el(c, "name").value;
    if (name == "") {
      alert("Name must not be empty");
      return;
    }
    cfg = {
      name: name,
      in_mode: parseInt(el(c, "in_mode").value),
      swap_inputs: el(c, "swap_inputs").checked,
      swap_outputs: el(c, "swap_outputs").checked,
    };
  }
  setComponentConfig(c, cfg, spinner);
}

function gdoSetConfig(c, cfg, spinner) {
  if (!cfg) {
    var name = el(c, "name").value;
    if (name == "") {
      alert("Name must not be empty");
      return;
    }
    var moveTime = parseInt(el(c, "move_time").value);
    if (isNaN(moveTime) || moveTime < 10) {
      alert(`Invalid movement time ${moveTime}`);
      return;
    }
    var pulseTimeMs = parseInt(el(c, "pulse_time_ms").value);
    if (isNaN(pulseTimeMs) || pulseTimeMs < 20) {
      alert(`Invalid pulse time ${pulseTimeMs}`);
      return;
    }
    cfg = {
      name: name,
      move_time: moveTime,
      pulse_time_ms: pulseTimeMs,
      close_sensor_mode: parseInt(el(c, "close_sensor_mode").value),
    };
    if (c.data.open_sensor_mode >= 0) {
      cfg.open_sensor_mode = parseInt(el(c, "open_sensor_mode").value);
    }
    if (c.data.out_mode >= 0) {
      cfg.out_mode = parseInt(el(c, "out_mode").value);
    }
  }
  setComponentConfig(c, cfg, spinner);
}

el("reboot_btn").onclick = function () {
  if(!confirm("Reboot the device?")) return;

  callDevice("Sys.Reboot", {delay_ms: 500}).then(function () {
    alert("System is rebooting and will reconnect when ready.");
  });
}

el("reset_btn").onclick = function () {
  if(!confirm("Device configuration will be wiped and return to AP mode. Are you sure?")) return;

  callDevice("Shelly.WipeDevice", {}).then(function () {
    alert("Device configuration has been reset, it will reboot in AP mode.");
  });
}

function findOrAddContainer(cd) {
  var elId = `c${cd.type}-${cd.id}`;
  var c = el(elId);
  if (c) return c;
  switch (cd.type) {
    case 0: // Switch
    case 1: // Outlet
    case 2: // Lock
      c = el("sw_template").cloneNode(true);
      c.id = elId;
      el(c, "state").onchange = function () {
        setComponentState(c, {state: !c.data.state}, el(c, "set_spinner"));
      };
      el(c, "save_btn").onclick = function () {
        swSetConfig(c);
      };
      el(c, "auto_off").onchange = function () {
        el(c, "auto_off_delay_container").style.display = this.checked ? "block" : "none";
      };
      break;
    case 3: // Stateless Programmable Switch (aka input in detached mode).
      c = el("ssw_template").cloneNode(true);
      c.id = elId;
      el(c, "save_btn").onclick = function () {
        sswSetConfig(c);
      };
      break;
    case 4: // Window Covering
      c = el("wc_template").cloneNode(true);
      c.id = elId;
      el(c, "open_btn").onclick = function () {
        setComponentState(c, {tgt_pos: 100}, el(c, "open_spinner"));
      };
      el(c, "close_btn").onclick = function () {
        setComponentState(c, {tgt_pos: 0}, el(c, "close_spinner"));
      };
      el(c, "set_tgt_pos_button").onclick = function () {
        setComponentState(c, {tgt_pos: el(c, "set_tgt_pos_input").value}, el(c, "close_spinner"));
      }
      el(c, "save_btn").onclick = function () {
        wcSetConfig(c, null, el(c, "save_spinner"))
      };
      el(c, "cal_btn").onclick = function () {
        setComponentState(c, {state: 10}, null);
        el(c, "cal_spinner").className = "spin";
      };
      break;
    case 5: // Garage Door Opener
      c = el("gdo_template").cloneNode(true);
      c.id = elId;
      el(c, "save_btn").onclick = function () {
        gdoSetConfig(c, null, el(c, "save_spinner"));
      };
      el(c, "toggle_btn").onclick = function () {
        setComponentState(c, {toggle: true}, el(c, "toggle_spinner"));
      };
      break;
    case 6: // Disabled Input.
      c = el("di_template").cloneNode(true);
      c.id = elId;
      el(c, "save_btn").onclick = function () {
        diSetConfig(c);
      };
      break;
    case 7: // Motion Sensor.
    case 8: // Occupancy Sensor.
    case 9: // Contact Sensor.
    case 10: // Doorbell
      c = el("sensor_template").cloneNode(true);
      c.id = elId;
      el(c, "save_btn").onclick = function () {
        mosSetConfig(c);
      };
      break;
    case 11: // RGB
      c = el("rgb_template").cloneNode(true);
      c.id = elId;
      el(c, "state").onchange = function () {
        setComponentState(c, rgbState(c, !c.data.state), el(c, "set_spinner"));
      };
      el(c, "save_btn").onclick = function () {
        rgbSetConfig(c);
      };
      el(c, "hue").onchange =
      el(c, "saturation").onchange =
      el(c, "brightness").onchange = function () {
        setComponentState(c, rgbState(c, c.data.state), el(c, "toggle_spinner"));
        setPreviewColor(c);
      };
      el(c, "auto_off").onchange = function () {
        el(c, "auto_off_delay_container").style.display = this.checked ? "block" : "none";
      };
      break;
    default:
      console.log(`Unhandled component type: ${cd.type}`);
  }
  if (c) {
    c.style.display = "block";
    el("components").appendChild(c);
  }
  return c;
}

function rgbState(c, newState) {
  return {
    state: newState,
    hue: el(c, "hue").value,
    saturation: el(c, "saturation").value,
    brightness: el(c, "brightness").value
  }
}

function updateComponent(cd) {
  var c = findOrAddContainer(cd);
  if (!c) return;
  switch (cd.type) {
    case 0: // Switch
    case 1: // Outlet
    case 2: // Lock
    case 11: // RGB
      var headText = `Switch ${cd.id}`;
      if (cd.name) headText += ` (${cd.name})`;
      el(c, "head").innerText = headText;
      setValueIfNotModified(el(c, "name"), cd.name);
      el(c, "state").checked = cd.state;
      if (cd.apower !== undefined) {
        el(c, "power_stats").innerText = `${Math.round(cd.apower)}W, ${cd.aenergy}Wh`;
        el(c, "power_stats_container").style.display = "block";
      }
      if (cd.svc_type !== undefined) {
        selectIfNotModified(el(c, "svc_type"), cd.svc_type);
        if (cd.svc_type == 3) {
          selectIfNotModified(el(c, "valve_type"), cd.valve_type);
          el(c, "valve_type_container").style.display = "block";
          el(c, "valve_type_label").innerText = "Valve Type:";
        } else {
          el(c, "valve_type_container").style.display = "none";
        }
      }
      selectIfNotModified(el(c, "initial"), cd.initial);
      if (cd.in_mode >= 0) {
        selectIfNotModified(el(c, "in_mode"), cd.in_mode);
        if (cd.in_mode != 3) {
          checkIfNotModified(el(c, "in_inverted"), cd.in_inverted);
          el(c, "in_inverted_container").style.display = "block";
        }
      } else {
        el(c, "in_mode_container").style.display = "none";
        el(c, "in_inverted_container").style.display = "none";
        if (el(c, "initial_3")) el(c, "initial_3").remove();
      }
      if (cd.out_inverted !== undefined) {
        checkIfNotModified(el(c, "out_inverted"), cd.out_inverted);
      }
      checkIfNotModified(el(c, "auto_off"), cd.auto_off);
      el(c, "auto_off_delay_container").style.display = el(c, "auto_off").checked ? "block" : "none";
      setValueIfNotModified(el(c, "auto_off_delay"), secondsToDateString(cd.auto_off_delay));
      if (cd.state_led_en !== undefined) {
        if (cd.state_led_en == -1) {
          el(c, "state_led_en_container").style.display = "none";
        } else {
          el(c, "state_led_en_container").style.display = "block";
          checkIfNotModified(el(c, "state_led_en"), cd.state_led_en == 1);
        }
      }

      if (cd.type == 11) { // RGB
        var headText = "RGB";
        if (cd.name) headText += ` (${cd.name})`;
        el(c, "head").innerText = headText;
        setValueIfNotModified(el(c, "name"), cd.name);
        el(c, "state").checked = cd.state;
        if (cd.apower !== undefined) {
          el(c, "power_stats").innerText = `${Math.round(cd.apower)}W, ${cd.aenergy}Wh`;
          el(c, "power_stats_container").style.display = "block";
        }
        slideIfNotModified(el(c, "hue"), cd.hue);
        slideIfNotModified(el(c, "saturation"), cd.saturation);
        slideIfNotModified(el(c, "brightness"), cd.brightness);
        setValueIfNotModified(el(c, "transition_time"), cd.transition_time);
        setPreviewColor(c);
      }
      break;
    case 3: // Stateless Programmable Switch (aka input in detached mode).
      var headText = `Input ${cd.id}`;
      if (cd.name) headText += ` (${cd.name})`;
      el(c, "head").innerText = headText;
      setValueIfNotModified(el(c, "name"), cd.name);
      selectIfNotModified(el(c, "in_mode"), cd.in_mode);
      selectIfNotModified(el(c, "type"), cd.type);
      checkIfNotModified(el(c, "inverted"), cd.inverted);
      var lastEvText = "n/a";
      if (cd.last_ev_age > 0) {
        var lastEv = cd.last_ev;
        switch (cd.last_ev) {
          case 0:
            lastEv = "single";
            break;
          case 1:
            lastEv = "double";
            break;
          case 2:
            lastEv = "long";
            break;
          default:
            lastEv = cd.last_ev;
        }
        lastEvText = `${lastEv} (${secondsToDateString(cd.last_ev_age)} ago)`;
      }
      el(c, "last_event").innerText = lastEvText;
      break;
    case 4: // Window Covering
      el(c, "head").innerText = cd.name;
      setValueIfNotModified(el(c, "name"), cd.name);
      el(c, "state").innerText = cd.state_str;
      selectIfNotModified(el(c, "in_mode"), cd.in_mode);
      checkIfNotModified(el(c, "swap_inputs"), cd.swap_inputs);
      checkIfNotModified(el(c, "swap_outputs"), cd.swap_outputs);
      if (cd.cal_done == 1) {
        if (cd.cur_pos != cd.tgt_pos) {
          el(c, "pos").innerText = `${cd.cur_pos} -> ${cd.tgt_pos}`;
        } else {
          el(c, "pos").innerText = cd.cur_pos;
        }
        el(c, "cal").innerText = `\
          movement time: ${cd.move_time_ms / 1000} s, \
          avg power: ${cd.move_power} W`;
        el(c, "pos_ctl").style.display = "block";
      } else {
        el(c, "pos").innerText = "n/a";
        el(c, "cal").innerText = "not calibrated";
        el(c, "pos_ctl").style.display = "none";
      }
      if (cd.state >= 10 && cd.state < 20) {  // Calibration is ongoing.
        el(c, "cal_spinner").className = "spin";
        el(c, "cal").innerText = "in progress";
      } else if (!(cd.state >= 20 && cd.state <= 25)) {
        el(c, "cal_spinner").className = "";
        el(c, "open_spinner").className = "";
        el(c, "close_spinner").className = "";
      }
      break;
    case 5: // Garage Doot Opener
      el(c, "head").innerText = cd.name;
      setValueIfNotModified(el(c, "name"), cd.name);
      el(c, "state").innerText = cd.cur_state_str;
      selectIfNotModified(el(c, "close_sensor_mode"), cd.close_sensor_mode);
      setValueIfNotModified(el(c, "move_time"), cd.move_time);
      setValueIfNotModified(el(c, "pulse_time_ms"), cd.pulse_time_ms);
      if (cd.open_sensor_mode >= 0) {
        selectIfNotModified(el(c, "open_sensor_mode"), cd.open_sensor_mode);
      } else {
        el(c, "open_sensor_mode_container").style.display = "none";
      }
      if (cd.out_mode >= 0) {
        selectIfNotModified(el(c, "out_mode"), cd.out_mode);
      } else {
        el(c, "out_mode_container").style.display = "none";
      }
      break;
    case 6: // Disabled Input
      var headText = `Input ${cd.id}`;
      el(c, "head").innerText = headText;
      selectIfNotModified(el(c, "type"), cd.type);
      break;
    case 7: // Motion Sensor
    case 8: // Occupancy Sensor
    case 9: // Contact Sensor
    case 10: // Doorbell
      var headText = `Input ${cd.id}`;
      if (cd.name) headText += ` (${cd.name})`;
      el(c, "head").innerText = headText;
      setValueIfNotModified(el(c, "name"), cd.name);
      selectIfNotModified(el(c, "type"), cd.type);
      checkIfNotModified(el(c, "inverted"), cd.inverted);
      selectIfNotModified(el(c, "in_mode"), cd.in_mode);
      setValueIfNotModified(el(c, "idle_time"), cd.idle_time);
      el(c, "idle_time_container").style.display = (cd.in_mode == 0 ? "none" : "block");
      var what = (cd.type == 7 ? "motion" : "occupancy");
      var statusText = (cd.state ? `${what} detected` : `no ${what} detected`);
      if (cd.last_ev_age > 0) {
        statusText += `; last ${secondsToDateString(cd.last_ev_age)} ago`;
      }
      el(c, "status").innerText = statusText;
      break;
    default:
      console.log(`Unhandled component type: ${cd.type}`);
  }
  c.data = cd;
}

function updateElement(key, value, info) {
  switch (key) {
    case "uptime":
      el("uptime").innerText = durationStr(value);
      break;
    case "model":
      if (value == "ShellyRGBW2") {
        el("sys_mode_container").style.display = "block";
        if (el("sys_mode_0")) el("sys_mode_0").remove();
      } else {
        if (el("sys_mode_3")) el("sys_mode_3").remove();
        if (el("sys_mode_4")) el("sys_mode_4").remove();
      }
      el(key).innerHTML = value;
      break;
    case "device_id":
    case "version":
      el(key).innerHTML = value;
      break;
    case "fw_build":
      el("fw_build").innerHTML = value;
      break;
    case "name":
      document.title = value;
      el("device_name").innerText = value;
      setValueIfNotModified(el("sys_name"), value);
      break;
    case "wifi_en":
      checkIfNotModified(el("wifi_en"), value);
      break;
    case "wifi_ssid":
      setValueIfNotModified(el("wifi_ssid"), value);
      break;
    case "wifi_pass":
      el("wifi_pass").placeholder = (value ? "(hidden)" : "(empty)");
      break;
    case "wifi_rssi":
    case "host":
      el(key).innerText = value;
      el(`${key}_container`).style.display = (value !== 0) ? "block" : "none";
      break;
    case "wifi_ip":
      if (value !== undefined) {
        // These only make sense if we are connected to WiFi.
        el("update_container").style.display = "block";
        el("revert_to_stock_container").style.display = "block";
        // We set external image URL to prevent loading it when not on WiFi, as it slows things down.
        if (el("donate_form_submit").src == "") {
          el("donate_form_submit").src = "https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif";
        }
        el("donate_form_submit").style.display = "inline";

        el("wifi_ip").innerText = value;
        el("wifi_container").style.display = "block";
      } else {
        el("wifi_ip").innerText = "Not connected";
      }
      break;
    case "hap_paired":
      if (value) {
        el(key).innerText = "yes";
        el("hap_setup_btn").style.display = "none";
        el("hap_reset_btn").style.display = "";
      } else {
        el(key).innerText = "no";
        el("hap_setup_btn").style.display = "";
        el("hap_reset_btn").style.display = "none";
      }
      break;
    case "hap_cn":
      if (value !== el("components").cn) {
        el("components").innerHTML = "";
      }
      el("components").cn = value;
      break;
    case "components":
      // the number of components has changed, delete them all and start afresh
      if (lastInfo !== null && lastInfo.components.length !== value.length) el("components").innerHTML = "";
      for (let i in value) updateComponent(value[i]);
      break;
    case "hap_running":
      if (!value) {
        el("hap_ip_conns_max").innerText = "server not running"
        el("hap_ip_conns_pending").style.display = "none";
        el("hap_ip_conns_active").style.display = "none";
      }
      break;
    case "hap_ip_conns_pending":
    case "hap_ip_conns_active":
    case "hap_ip_conns_max":
      if (info.hap_running) {
        el(key).style.display = "inline";
        el(key).innerText = `${value} ${key.split("_").slice(-1)[0]}`;
      }
      break;
    case "wc_avail":
      if (value) el("sys_mode_container").style.display = "block";
      else if (el("sys_mode_1")) el("sys_mode_1").remove();
      break;
    case "gdo_avail":
      if (value) el("sys_mode_container").style.display = "block";
      else if (el("sys_mode_2")) el("sys_mode_2").remove();
      break;
    case "sys_mode":
      selectIfNotModified(el("sys_mode"), value);
      break;
    case "sys_temp":
      if (value !== undefined) {
        el("sys_temp").innerText = value;
        el("sys_temp_container").style.display = "block";
      } else {
        el("sys_temp_container").style.display = "none";
      }
      break;
    case "overheat_on":
      el("notify_overheat").style.display = (value ? "inline" : "none");
      break;
    case "ota_progress":
      if (value >= 0) {
        el("update_status").innerText = `${value}%`;
      }
      break;
  }
}

function getInfo() {
  return new Promise(function (resolve, reject) {
    let method = (infoLevel == 1 ? "Shelly.GetInfoExt" : "Shelly.GetInfo");

    callDevice(method).then(function (res) {
      var info = res.result;

      if (!info) {
        reject();
        return;
      }

      // Update the essentials.
      ["name", "model", "device_id", "version", "fw_build"].forEach((key) => {
        updateElement(key, info[key], info);
      });
      if (info.failsafe_mode) {
        el("sys_container").style.display = "block";
        el("firmware_container").style.display = "block";
        el("notify_failsafe").style.display = "inline";
        pauseAutoRefresh = true;
        reject();
        return;
      }

      if (infoLevel == 0) {
        infoLevel++;
        // Get extended info.
        resolve(getInfo());
        return;
      }

      lastInfo = info;

      el("sec_old_pass_container").style.display = (info.auth_en ? "block" : "none");
      el("homekit_container").style.display = "block";
      el("sec_container").style.display = "block";
      el("sys_container").style.display = "block";
      el("firmware_container").style.display = "block";
      el("gs_container").style.display = "block";

      // the system mode changed, clear out old UI components
      if (lastInfo !== null && lastInfo.sys_mode !== info.sys_mode) {
        el("components").innerHTML = "";
      }

      for (let element in info) {
        updateElement(element, info[element], info);
      }

      resolve(info);
    }).catch(function (err) {
      console.log(err);
      infoLevel = 0;
      reject(err);
    });
  });
}

function getVar(key) {
  let vs = window.localStorage.getItem(key);
  if (!vs) return undefined;
  let v = JSON.parse(vs);
  if (v.exp !== undefined && (new Date()).getTime() > v.exp) {
    console.log("Expired", key);
    localStorage.removeItem(key)
    return undefined;
  }
  return v.value;
}

function setVar(key, value, maxAge) {
  if (value === undefined) {
    console.log("Delete var", key);
    localStorage.removeItem(key);
    return;
  }
  let v = {value: value};
  if (maxAge > 0) {
    v.exp = (new Date()).getTime() + (maxAge * 1000);
  }
  console.log("SetVar", key, v, maxAge);
  window.localStorage.setItem(key, JSON.stringify(v));
  // Clear out cookie storage. Added: 2021/04/24.
  document.cookie = `${key}=;max-age=1`;
}

function setupHost() {
  host = (new URLSearchParams(location.search)).get("host") || location.host;

  if (!host) {
    host = prompt("Please enter the host of your shelly.");
    if (host !== null) {
      location.href = `${location.host}?host=${host}`;
    }
  }

  el("debug_link").href = `http://${host}/debug/log?follow=1`;
}

function connectWebSocket() {
  setupHost();

  return new Promise(function(resolve, reject) {
    socket = new WebSocket(`ws://${host}/rpc`);
    connectionTries += 1;

    socket.onclose = function(event) {
      console.log(`[close] Connection died (code ${event.code})`);
      el("notify_disconnected").style.display = "inline"
      // attempt to reconnect
      setTimeout(function() {
        connectWebSocket()
          // reload the page once we reconnect (the web ui could have changed)
          .then(() => location.reload())
          .catch(() => console.log("[error] Could not reconnect to Shelly"));
      }, Math.min(3000, connectionTries * 1000));
    };

    socket.onerror = function(error) {
      el("notify_disconnected").style.display = "inline"
      let pr = pendingRequests;
      pendingRequests = {};
      for (let id in pr) {
        pr[id].reject(error);
      }
      reject(error);
    };

    socket.onopen = function() {
      console.log("[open] Connection established");
      el("notify_disconnected").style.display = "none";
      connectionTries = 0;
      resolve(socket);
    };

    socket.onmessage = function(event) {
      let resp = JSON.parse(event.data);
      let id = resp.id;
      let ri = pendingRequests[id];
      if (!ri) return;
      delete pendingRequests[id];
      console.log(`[<-] ${id} ${ri.method}`, ri, resp);
      if (resp.error && resp.error.code == 401) {
        rpcAuth = null;
        let authReq = JSON.parse(resp.error.message);
        let ar = null;
        if (!ri.ar) {
          authRealm = authReq.realm;
          ar = getAuthResp(authReq);
          console.log(`Auth required for ${ri.method}`, authReq, ar);
        } else {
          console.log(`Auth failed for ${ri.method}`, authReq);
          el("forgot_password").style.display = "block";
          setVar(authInfoKey, undefined);
        }
        if (ar) {
          console.log("Retrying with auth...");
          callDeviceAuth(ri.method, ri.params, ar)
              .then(function (resp) { ri.resolve(resp); })
              .catch(function (err) { ri.reject(err); });
        } else {
          if (lastInfo !== null) {
            // Locked out, reload UI.
            location.reload();
          } else {
            pauseAutoRefresh = true;
            el("auth_container").style.display = "block";
            el("auth_pass").focus();
            ri.reject(new Error("Please log in"));
          }
        }
      } else {
        if (ri.ar) {
          el("auth_container").style.display = "none";
          pauseAutoRefresh = false;
          setVar(authInfoKey, ri.ar.ai, maxAuthAge);
          rpcAuth = ri.ar.rpcAuth;
        }
        ri.resolve(resp);
      }
    };

  });
}

// Implementation of the digest auth algorithm with SHA-256 (RFC 7616).
// We have to do it manually because browsers still don't support it natively.
function calcHA1(user, realm, pass) {
  return sha256(`${user}:${realm}:${pass}`);
}

function getAuthResp(req) {
  const method = req.method || "dummy_method";
  const uri = req.uri || "dummy_uri";
  const cnonce = "" + Math.ceil(Math.random() * 100000000);
  const qop = "auth";
  let ha1 = "";
  if (el("auth_pass").value !== "") {
    ha1 = calcHA1(authUser, req.realm, el("auth_pass").value);
    el("auth_pass").value = "";
  } else {
    let ai = getVar(authInfoKey);
    if (ai && ai.realm == req.realm) {
      ha1 = ai.ha1;
    }
  }
  if (!ha1) return null;
  if (req.algorithm != "SHA-256") return null;
  const ha2 = sha256(`${method}:${uri}`);
  const resp = sha256(`${ha1}:${req.nonce}:${req.nc}:${cnonce}:${qop}:${ha2}`);
  let authResp = {
    rpcAuth: {
      realm: req.realm,
      nonce: req.nonce,
      username: authUser,
      cnonce: cnonce,
      algorithm: req.algorithm,
      response: resp,
    },
    httpAuth: (`Digest realm="${req.realm}", uri="${uri}", username="${authUser}", ` +
               `cnonce="${cnonce}", qop=${qop}, nc=${req.nc}, nonce="${req.nonce}", ` +
               `response="${resp}", algorithm=${req.algorithm}`),
    ai: {
      realm: req.realm,
      ha1: ha1,
    },
  };
  if (req.opaque) {
    authResp.rpcAuth.opaque = req.opaque;
    authResp.http_auth += `, opaque="${req.opaque}"`;
  };
  return authResp;
}

function authHeaderToReq(method, uri, hdr, nc) {
  let authReq = {
      method: method,
      uri: uri,
      nc: nc,
      realm: /realm="([^"]+)"/.exec(hdr)[1],
      nonce: /nonce="([^"]+)"/.exec(hdr)[1],
      algorithm: /algorithm=([A-Za-z0-9-]+)/.exec(hdr)[1],
  };
  let opq = /opaque="([^"]+)"/.exec(hdr);
  if (opq !== null) {
    authReq.opaque = opq[1];
  }
  return authReq;
}

function callDeviceAuth(method, params, ar) {
  let id = nextRequestID++;
  return new Promise(function(resolve, reject) {
    try {
      console.log(`[->] ${id} ${method}`, params, ar);
      let frame = {
          "id": id,
          "method": method,
          "params": params,
      };
      if (ar) {
        frame.auth = ar.rpcAuth;
      } else if (rpcAuth) {
        frame.auth = rpcAuth;
      }
      socket.send(JSON.stringify(frame));
      pendingRequests[id] = {
          method: method,
          params: params,
          ar: ar,
          resolve: resolve,
          reject: reject,
      };
    } catch (e) {
      reject(e);
    }
  });
}

function callDevice(method, params = []) {
  return callDeviceAuth(method, params, null);
}

function doLogin() {
  el("auth_log_in_spinner").className = "spin";
  getInfo().finally(() => el("auth_log_in_spinner").className = "");
}

el("auth_log_in_btn").onclick = function () {
  doLogin();
  return true;
};

el("auth_pass").onkeyup = function (e) {
  console.log(e);
  if (e.code == "Enter") doLogin();
  return false;
};

el("sec_log_out_btn").onclick = function () {
  setVar(authInfoKey, undefined);
  location.reload();
  return true;
};

el("sec_save_btn").onclick = function () {
  if (authRealm !== null) {
    let oldHA1 = calcHA1(authUser, authRealm, el("sec_old_pass").value);
    let goodHA1 = getVar(authInfoKey).ha1;
    if (oldHA1 !== goodHA1) {
      alert("Invalid old password!");
      setVar(authInfoKey, undefined);
      rpcAuth = null;
      return;
    }
  }
  let newHA1 = "";
  let realm = lastInfo.device_id;
  if (el("sec_new_pass").value !== "") {
    newHA1 = calcHA1(authUser, realm, el("sec_new_pass").value);
    if (!newHA1) {
      alert("No unicode in passwords please");
      return true;
    }
  }
  pauseAutoRefresh = true;
  el("sec_save_spinner").className = "spin";
  callDevice("Shelly.SetAuth", {user: authUser, realm: realm, ha1: newHA1}).then(function () {
    setVar(authInfoKey, undefined);
    location.reload();
  }).catch(function (err) {
    if (err.response) err = err.response.data.message;
    alert(err);
  }).finally(function() {
    el("sec_save_spinner").className = "";
    pauseAutoRefresh = false;
  });
  return true;
};

// noinspection JSUnusedGlobalSymbols
function onLoad() {
  connectWebSocket().then(() => {
    getInfo().catch((err) => {
      console.log("getInfo() rejected", err);
    });
  });
  setInterval(refreshUI, 1000);
}

function refreshUI() {
  // if the socket is open and connected and the page is visible to the user
  if (document.hidden) return;
  if (socket.readyState !== 1) return;
  if (pauseAutoRefresh || pendingGetInfo) return;
  pendingGetInfo = true;
  getInfo()
    .then((info) => checkUpdateIfNeeded(info))
    .finally(() => pendingGetInfo = false);
}

function setValueIfNotModified(e, newValue) {
  // do not update the value of the input field if
  if (e.selected ||                    // the user has selected / highlighted the input field OR
    e.lastSetValue === e.value ||      // the value has not been changed by the user OR
    (e.lastSetValue !== undefined &&   // a value has previously been set AND
      e.lastSetValue !== e.value))     // it is not currently the same as the visible value
    return;
  e.value = e.lastSetValue = newValue;
}

function checkIfNotModified(e, newState) {
  // do not update the checked value if
  if (e.lastSetValue === e.checked ||  // the value has not changed (unnecessary) OR
    (e.lastSetValue !== undefined &&   // a value has previously been set AND
      e.lastSetValue !== e.checked))   // it is not currently the same as the visible value
    return;
  e.checked = e.lastSetValue = newState;
}

function slideIfNotModified(e, newValue) {
  // do not update the value of the input field if
  if (e.lastSetValue === e.value &&    // the value has not been changed by the user AND
    (e.lastSetValue !== undefined &&   // a value has previously been set AND
      e.lastSetValue !== e.value))     // it is not currently the same as the visible value
    return;
  e.value = e.lastSetValue = newValue.toString();
}

function selectIfNotModified(e, newSelection) {
  setValueIfNotModified(e, newSelection);
}


function durationStr(d) {
  var days = parseInt(d / 86400);
  d %= 86400;
  var hours = parseInt(d / 3600);
  d %= 3600;
  var mins = parseInt(d / 60);
  var secs = d % 60;
  return days + ":" +
    nDigitString(hours, 2) + ":" +
    nDigitString(mins, 2) + ":" +
    nDigitString(secs, 2);
}

async function downloadUpdate(fwURL, spinner, status) {
  spinner.className = "spin";
  status.innerText = "Downloading...";
  console.log("Downloading", fwURL);
  fetch(fwURL, {mode: "cors"})
    .then(async (resp) => {
      console.log(resp);
      var blob = await resp.blob();
      if (!resp.ok || blob.type != "application/zip") {
        status.innerText = "Failed, try manually.";
        return;
      }
      return uploadFW(blob, spinner, status);
    }).catch((error) => {
    spinner.className = "";
    status.innerText = `Error downloading: ${error}`;
  });
}

async function uploadFW(blob, spinner, status, ar) {
  spinner.className = "spin";
  status.innerText = `Uploading ${blob.size} bytes...`;
  let fd = new FormData();
  fd.append("file", blob);
  let hd = new Headers();
  if (ar) {
    hd.append("Authorization", ar.httpAuth);
  }
  fetch("/update", {
      method: "POST",
      mode: "cors",
      headers: hd,
      body: fd,
      cache: "no-cache",
  })
    .then(async (resp) => {
      let respText = await resp.text();
      console.log("resp", resp, respText, resp.status, ar);
      if (resp.status == 401 && !ar) {
        let authHdr = resp.headers.get("www-authenticate");
        if (authHdr !== null) {
          let authReq = authHeaderToReq("POST", "/update", authHdr, "00000001");
          let authResp = getAuthResp(authReq);
          console.log("Retrying with auth...");
          return uploadFW(blob, spinner, status, authResp);
        }
      }
      spinner.className = "";
      status.innerText = (respText ? respText : resp.statusText);
      setVar("update_available", false);
    })
    .catch((error) => {
      console.log("Fetch erorr:", error);
      status.innerText = `Error uploading: ${error}`;
      spinner.className = "";
    });
}

// major.minor.patch-variantN
function parseVersion(versionString) {
  version = versionString.match(/^(?<major>\d+).(?<minor>\d+).(?<patch>\d+)-?(?<variant>[a-z]*)(?<varSeq>\d*)$/).groups
  version.major = parseInt(version.major);
  version.minor = parseInt(version.minor);
  version.patch = parseInt(version.patch);
  version.varSeq = parseInt(version.varSeq) || 0;

  return version;
}

function isNewer(v1, v2) {
  var vi1 = parseVersion(v1), vi2 = parseVersion(v2);
  if (vi1.major != vi2.major) return (vi1.major > vi2.major);
  if (vi1.minor != vi2.minor) return (vi1.minor > vi2.minor);
  if (vi1.patch != vi2.patch) return (vi1.patch > vi2.patch);
  if (vi1.variant != vi2.variant) return true;
  if (vi1.varSeq != vi2.varSeq) return (vi1.varSeq > vi2.varSeq);
  return false;
}

function checkUpdateIfNeeded(info) {
  // If device is in AP mode, we most likely don't have internet connectivity anyway.
  if (info.wifi_rssi == 0) return;
  let last_update_check = parseInt(getVar("last_update_check"));
  let now = new Date();
  let age = undefined;
  if (!isNaN(last_update_check)) {
    age = (now.getTime() - last_update_check) / 1000;
  }
  if (isNaN(last_update_check) || age > updateCheckInterval) {
    console.log(`Last update check: ${last_update_check} age ${age}, checking for update`);
    checkUpdate();
  }
  el("notify_update").style.display = (getVar("update_available") ? "inline" : "none");
}

function checkUpdate() {
  var model = lastInfo.model;
  var curVersion = lastInfo.version;
  var e = el("update_status");
  var se = el("update_btn_spinner");
  var errMsg = 'Failed, check <a href="https://github.com/mongoose-os-apps/shelly-homekit/releases">GitHub</a>.';
  e.innerText = "";
  se.className = "spin";
  console.log("Model:", model, "Version:", curVersion);
  fetch("https://rojer.me/files/shelly/update.json",
    {
      headers: {
        "X-Model": model,
        "X-Current-Version": curVersion,
        "X-Current-Build": lastInfo.fw_build,
        "X-Device-ID": lastInfo.device_id,
      }
    })
    .then(resp => resp.json())
    .then((resp) => {
      // save the cookie before anything else, so that if update not
      // found we still remember that we tried to check for an update
      setVar("last_update_check", (new Date()).getTime());

      var cfg, latestVersion, updateURL, relNotesURL;
      for (var i in resp) {
        var re = new RegExp(resp[i][0]);
        if (curVersion.match(re)) {
          cfg = resp[i][1];
          break;
        }
      }
      if (cfg) {
        latestVersion = cfg.version;
        relNotesURL = cfg.rel_notes;
        if (cfg.urls) updateURL = cfg.urls[model];
      }
      console.log("Version:", latestVersion, "URL:", updateURL);
      if (!latestVersion || !updateURL) {
        console.log("Update section not found:", model, curVersion, cfg);
        e.innerHTML = errMsg;
        se.className = "";
        return;
      }
      var updateAvailable = isNewer(latestVersion, curVersion);
      el("notify_update").style.display = (updateAvailable ? "inline" : "none");

      setVar("update_available", updateAvailable);
      if (!updateAvailable) {
        e.innerText = "Up to date";
        se.className = "";
        return;
      }
      se.className = "";
      e.innerHTML = `
        Version ${latestVersion} is available.
        See <a href="${relNotesURL}" target="_blank">release notes</a>.`
      el("update_btn_text").innerText = "Install";
      el("update_btn").onclick = function () {
        return downloadUpdate(updateURL, el("update_btn_spinner"), el("update_status"));
      };
    })
    .catch((error) => {
      console.log("Error", error);
      e.innerHTML = errMsg;
      se.className = "";
    });
}

el("update_btn").onclick = function () {
  checkUpdate();
};
el("revert_btn").onclick = function () {
  if(!confirm("Revert to stock firmware?")) return;

  el("revert_msg").style.display = "block";
  var stockURL = `https://rojer.me/files/shelly/stock/${lastInfo.stock_fw_model}.zip`;
  downloadUpdate(stockURL, el("revert_btn_spinner"), el("revert_status"));
};

function setPreviewColor(c) {
  var h = el(c, "hue").value / 360;
  var s = el(c, "saturation").value / 100;

  // use fixed 100% for v, because we want to control brightness over pwm frequence
  var [r, g, b] = hsv2rgb(h, s, 100);

  r = Math.round(r * 2.55);
  g = Math.round(g * 2.55);
  b = Math.round(b * 2.55);

  rgbHex = [r, g, b].map(x => nDigitString(x.toString(16), 2)).join('').toUpperCase();

  el(c, "color_preview").style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
  el(c, "color_name").innerHTML = `#${rgbHex}`;
  el(c, "hue_value").innerHTML = `${el(c, "hue").value}&#176;`;
  el(c, "saturation_value").innerHTML = `${el(c, "saturation").value}%`;
  el(c, "brightness_value").innerHTML = `${el(c, "brightness").value}%`;
}

function hsv2rgb(h, s, v) {
  if(s == 0.0)
    return [v, v, v];

  i = parseInt(h * 6.0);
  f = (h * 6.0) - i;
  p = v * (1.0 - s);
  q = v * (1.0 - s * f);
  t = v * (1.0 - s * (1.0 - f));
  i = i % 6;

  switch(i) {
  case 0:
    return [v, t, p];
  case 1:
    return [q, v, p];
  case 2:
    return [p, v, t];
  case 3:
    return [p, q, v];
  case 4:
    return [t, p, v];
  case 5:
    return [v, p, q];
  }
}
