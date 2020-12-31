var lastInfo = null;

var socket = null;

var autoRefresh = true;

var wifiEn = el("wifi_en");
var wifiSSID = el("wifi_ssid");
var wifiPass = el("wifi_pass");
var wifiSpinner = el("wifi_spinner");

var hapSetupCode = el("hap_setup_code");
var hapSaveSpinner = el("hap_save_spinner");
var hapResetSpinner = el("hap_reset_spinner");

var sw1 = el("sw1_container");
var sw2 = el("sw2_container");

function el(container, id) {
  if (id === undefined) {
    id = container;
    container = document;
  }
  return container.querySelector("#" + id);
}

function checkName(name) {
  var ok = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-";
  if (name.length === 0 || name.length > 63) return false;
  for (var i in name) {
    if (ok.indexOf(name[i]) < 0) return false;
  }
  return true;
}

el("sys_save_btn").onclick = function () {
  if (!checkName(el("sys_name").value)) {
    alert("Name must be between 1 and 63 characters " +
      "and consist of letters, numbers or dashes ('-')");
    return;
  }
  var data = {
    config: {
      name: el("sys_name").value,
      sys_mode: parseInt(el("sys_mode").value),
    },
  };
  console.log("sysSetCfg:", data);
  el("sys_save_spinner").className = "spin";
  sendMessageWebSocket("Shelly.SetConfig", data).then(function () {
    el("sys_save_spinner").className = "";
    setTimeout(getInfo, 1100);
  }).catch(function (err) {
    el("sys_save_spinner").className = "";
    if (err.response) {
      err = err.response.data.message;
    }
    alert(err);
  });
};

el("hap_save_btn").onclick = function () {
  var code = hapSetupCode.value;
  if (!code.match(/^\d\d\d-\d\d-\d\d\d$/)) {
    if (code.match(/^\d\d\d\d\d\d\d\d$/)) {
      code = code.substr(0, 3) + "-" + code.substr(3, 2) + "-" + code.substr(5, 3);
    } else {
      alert("Invalid code '" + code + "', must be xxxyyzzz or xxx-yy-zzz.");
      return;
    }
  }
  hapSaveSpinner.className = "spin";
  sendMessageWebSocket("HAP.Setup", {"code": code})
    .catch(function (err) {
    if (err.response) {
      err = err.response.data.message;
    }
    alert(err);
  }).then(function () {
    hapSaveSpinner.className = "";
    getInfo();
  });
};

el("hap_reset_btn").onclick = function () {
  hapResetSpinner.className = "spin";
  sendMessageWebSocket("HAP.Reset", {"reset_server": true, "reset_code": true})
    .catch(function (err) {
    if (err.response) {
      err = err.response.data.message;
    }
    alert(err);
  }).then(function () {
    hapResetSpinner.className = "";
    getInfo();
  });
};

el("fw_upload_btn").onclick = function () {
  el("fw_spinner").className = "spin";
  el("fw_upload_form").submit();
  return true;
};

el("wifi_save_btn").onclick = function () {
  wifiSpinner.className = "spin";
  var data = {
    config: {
      wifi: {
        sta: {enable: wifiEn.checked, ssid: wifiSSID.value},
        ap: {enable: !wifiEn.checked},
      },
    },
    reboot: true,
  };
  if (wifiPass.value != "***") data.config.wifi.sta.pass = wifiPass.value;
  sendMessageWebSocket("Config.Set", data).then(function () {
    var dn = el("device_name").innerText;
    document.body.innerHTML =
      "<div class='container'><h1>Rebooting...</h1>" +
      "<p>Device is rebooting and connecting to <i>" + wifiSSID.value + "</i>." +
      "<p>Connect to the same network and visit " +
      "<a href='http://" + dn + ".local/'>http://" + dn + ".local/</a>." +
      "<p>If device cannot be contacted, see " +
      "<a href='https://github.com/mongoose-os-apps/shelly-homekit/wiki/Recovery'>here</a> for recovery options." +
      "</div>.";
  }).catch(function (err) {
    if (err.response) {
      err = err.response.data.message;
    }
    alert(err);
  }).then(function () {
    wifiSpinner.className = "";
  });
};

function setComponentConfig(c, cfg, spinner) {
  if (spinner) spinner.className = "spin";
  var data = {
    id: c.data.id,
    type: c.data.type,
    config: cfg,
  };
  console.log("SetConfig:", data);
  sendMessageWebSocket("Shelly.SetConfig", data)
    .then(function () {
      if (spinner) spinner.className = "";
      setTimeout(getInfo, 1100);
    }).catch(function (err) {
    if (spinner) spinner.className = "";
    if (err.response) {
      err = err.response.data.message;
    }
    alert(err);
  });
}

function setComponentState(c, state, spinner) {
  if (spinner) spinner.className = "spin";
  var data = {
    id: c.data.id,
    type: c.data.type,
    state: state,
  };
  console.log("SetState:", data);
  sendMessageWebSocket("Shelly.SetState", data)
    .then(function () {
      if (spinner) spinner.className = "";
      setTimeout(getInfo, 100);
    }).catch(function (err) {
    if (spinner) spinner.className = "";
    if (err.response) {
      err = err.response.data.message;
    }
    alert(err);
  });
}

function autoOffDelayValid(value) {
  return (dateStringToSeconds(value) >= 0.010) &&
    (dateStringToSeconds(value) <= 2147483.647);
}

function dateStringToSeconds(dateString) {
  if (dateString == "") return 0;
  var dateStringParts = dateString.split(':');
  var secondsPart = dateStringParts[3].split('.')[0];
  var fractionPart = dateStringParts[3].split('.')[1];
  var seconds = parseInt(dateStringParts[0]) * 24 * 3600 +
    parseInt(dateStringParts[1]) * 3600 +
    parseInt(dateStringParts[2]) * 60 +
    parseInt(secondsPart) +
    parseFloat(fractionPart / 1000);
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

function swSetConfig(c) {
  var name = el(c, "name").value;
  var svcType = el(c, "svc_type").value;
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
  };
  if (autoOff) {
    cfg.auto_off_delay = dateStringToSeconds(autoOffDelay);
  }
  if (c.data.in_mode >= 0) {
    cfg.in_mode = parseInt(el(c, "in_mode").value);
  }
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
      alert("Invalid movement time " + moveTime);
      return;
    }
    var pulseTimeMs = parseInt(el(c, "pulse_time_ms").value);
    if (isNaN(pulseTimeMs) || pulseTimeMs < 20) {
      alert("Invalid pulse time " + pulseTimeMs);
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
  sendMessageWebSocket("Sys.Reboot", {delay_ms: 500}).then(function () {
    alert("System is rebooting and will reconnect when ready.");
  });
}

function findOrAddContainer(cd) {
  var elId = "c" + cd.type + "-" + cd.id;
  var c = el(elId);
  if (c) return c;
  switch (cd.type) {
    case 0: // Switch
    case 1: // Outlet
    case 2: // Lock
      c = el("sw_template").cloneNode(true);
      c.id = elId;
      el(c, "toggle_btn").onclick = function () {
        setComponentState(c, {state: !c.data.state}, el(c, "set_spinner"));
      };
      el(c, "save_btn").onclick = function () {
        swSetConfig(c);
      };
      el(c, "auto_off").onchange = function () {
        el(c, "auto_off_delay").disabled = !this.checked;
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
      c = el("sensor_template").cloneNode(true);
      c.id = elId;
      el(c, "save_btn").onclick = function () {
        mosSetConfig(c);
      };
      break;
  }
  if (c) {
    c.style.display = "block";
    el("components").appendChild(c);
  }
  return c;
}

function updateComponent(cd) {
  var c = findOrAddContainer(cd);
  if (!c) return;
  switch (cd.type) {
    case 0: // Switch
    case 1: // Outlet
    case 2: // Lock
      var headText = "Switch " + cd.id;
      if (cd.name) headText += " (" + cd.name + ")";
      el(c, "head").innerText = headText;
      el(c, "name").value = cd.name;
      el(c, "state").innerText = (cd.state ? "on" : "off");
      if (cd.apower !== undefined) {
        el(c, "power_stats").innerText = ", " + Math.round(cd.apower) + "W, " + cd.aenergy + "Wh";
      }
      el(c, "btn_label").innerText = "Turn " + (cd.state ? "Off" : "On");
      el(c, "svc_type_" + cd.svc_type).selected = true;
      el(c, "initial_" + cd.initial).selected = true;
      if (cd.in_mode >= 0) {
        el(c, "in_mode_" + cd.in_mode).selected = true;
        if (cd.in_mode != 3) {
          el(c, "in_inverted").checked = cd.in_inverted;
          el(c, "in_inverted_container").style.display = "block";
        }
      } else {
        el(c, "in_mode_container").style.display = "none";
        el(c, "in_inverted_container").style.display = "none";
        if (el(c, "initial_3")) el(c, "initial_3").remove();
      }
      el(c, "auto_off").checked = cd.auto_off;
      el(c, "auto_off_delay").disabled = !cd.auto_off;
      el(c, "auto_off_delay").value = secondsToDateString(cd.auto_off_delay);
      break;
    case 3: // Stateless Programmable Switch (aka input in detached mode).
      var headText = "Input " + cd.id;
      if (cd.name) headText += " (" + cd.name + ")";
      el(c, "head").innerText = headText;
      el(c, "name").value = cd.name;
      el(c, "in_mode_" + cd.in_mode).selected = true;
      el(c, "type_" + cd.type).selected = true;
      el(c, "inverted").checked = cd.inverted;
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
        lastEvText = lastEv + " (" + secondsToDateString(cd.last_ev_age) + " ago)";
      }
      el(c, "last_event").innerText = lastEvText;
      break;
    case 4: // Window Covering
      el(c, "head").innerText = cd.name;
      el(c, "name").value = cd.name;
      el(c, "state").innerText = cd.state_str;
      el(c, "in_mode_" + cd.in_mode).selected = true;
      el(c, "swap_inputs").checked = cd.swap_inputs;
      el(c, "swap_outputs").checked = cd.swap_outputs;
      if (cd.cal_done == 1) {
        if (cd.cur_pos != cd.tgt_pos) {
          el(c, "pos").innerText = cd.cur_pos + " -> " + cd.tgt_pos;
        } else {
          el(c, "pos").innerText = cd.cur_pos;
        }
        el(c, "cal").innerText = "movement time: " + cd.move_time_ms / 1000 + " s, " +
          "avg power: " + cd.move_power + "W";
        el(c, "pos_ctl").style.display = "block";
      } else {
        el(c, "pos").innerText = "n/a";
        el(c, "cal").innerText = "not calibrated";
        el(c, "pos_ctl").style.display = "none";
      }
      if (cd.state >= 10 && cd.state < 20) {  // Calibration is ongoing.
        el(c, "cal_spinner").className = "spin";
        el(c, "cal").innerText = "in progress";
        autoRefresh = true;
      } else if (cd.state >= 20 && cd.state <= 25) {
        autoRefresh = true;
      } else {
        el(c, "cal_spinner").className = "";
        el(c, "open_spinner").className = "";
        el(c, "close_spinner").className = "";
      }
      break;
    case 5: // Garage Doot Opener
      el(c, "head").innerText = cd.name;
      el(c, "name").value = cd.name;
      el(c, "state").innerText = cd.cur_state_str;
      el(c, "close_sensor_mode_" + cd.close_sensor_mode).selected = true;
      el(c, "move_time").value = cd.move_time;
      el(c, "pulse_time_ms").value = cd.pulse_time_ms;
      if (cd.open_sensor_mode >= 0) {
        el(c, "open_sensor_mode_" + cd.open_sensor_mode).selected = true;
      } else {
        el(c, "open_sensor_mode_container").style.display = "none";
      }
      if (cd.out_mode >= 0) {
        el(c, "out_mode_" + cd.out_mode).selected = true;
      } else {
        el(c, "out_mode_container").style.display = "none";
      }
      break;
    case 6: // Disabled Input
      var headText = "Input " + cd.id;
      el(c, "head").innerText = headText;
      el(c, "type_" + cd.type).selected = true;
      break;
    case 7: // Motion Sensor
    case 8: // Occupancy Sensor
    case 9: // Contact Sensor
      var headText = "Input " + cd.id;
      if (cd.name) headText += " (" + cd.name + ")";
      el(c, "head").innerText = headText;
      el(c, "name").value = cd.name;
      el(c, "type_" + cd.type).selected = true;
      el(c, "inverted").checked = cd.inverted;
      el(c, "in_mode_" + cd.in_mode).selected = true;
      el(c, "idle_time").value = cd.idle_time;
      el(c, "idle_time_container").style.display = (cd.in_mode == 0 ? "none" : "block");
      var what = (cd.type == 7 ? "motion" : "occupancy");
      var statusText = (cd.state ? what + " detected" : "no " + what + " detected");
      if (cd.last_ev_age > 0) {
        statusText += "; last " + secondsToDateString(cd.last_ev_age) + " ago";
      }
      el(c, "status").innerText = statusText;
      break;
  }
  c.data = cd;
}

function updateElement(key, value) {
  switch (key) {
    case "uptime":
      el("uptime").innerText = durationStr(value);
      el("uptime_label").style.visibility = "visible";
      break;
    case "model":
    case "device_id":
    case "version":
    case "fw_build":
      el(key).innerText = value;
      break;
    case "name":
      el("device_name").innerText = el("sys_name").value = document.title = value;
      el("device_name").style.visibility = "visible";
      break;
    case "wifi_en":
      wifiEn.checked = value;
      break;
    case "wifi_ssid":
      wifiSSID.value = value;
      break;
    case "wifi_pass":
      wifiPass.value = value;
      break;
    case "wifi_rssi":
      if (value !== 0) {
        el("wifi_rssi").innerText = "RSSI: " + value;
        el("wifi_rssi").style.display = "inline";
      } else el("wifi_rssi").style.display = "none";
      break;
    case "wifi_ip":
      if (value !== undefined) {
        // These only make sense if we are connected to WiFi.
        el("update_container").style.display = "block";
        el("revert_to_stock_container").style.display = "block";
        // We set external image URL to prevent loading it when not on WiFi, as it slows things down.
        el("donate_form_submit").src = "https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif";
        el("donate_form_submit").style.display = "inline";

        el("wifi_ip").innerText = "IP: " + value;
        el("wifi_container").style.display = "block";
      } else {
        el("wifi_ip").innerText = "Not connected";
      }
      break;
    case "host":
      if (value !== "") {
        el("host").innerText = "Host: " + value;
        el("host").style.display = "inline";
      } else el("host").style.display = "none";
      break;
    case "hap_paired":
      el(key).innerText = (value ? "yes" : "no");
      break;
    case "hap_cn":
      if (value !== el("components").cn) {
        el("components").innerHTML = "";
      }
      el("components").cn = value;
      break;
    case "components":
      for (let i in value) {
        let update = false;
        if (lastInfo !== null) {
          for (let comp_el in value[i]) {
            if (lastInfo.components[i][comp_el] !== value[i][comp_el]) {
              update = true;
              break;
            }
          }
        }
        if (lastInfo === null || update) updateComponent(value[i]);
      }
      break;
    case "hap_running":
      hapSetupCode.value = value ? "***-**-***" : "";
      if (!value)
        el("hap_ip_conns_max").innerText = "Server not running"
        el("hap_ip_conns_pending").style.display
        = el("hap_ip_conns_active").style.display
        = "none";
      break;
    case "hap_ip_conns_pending":
    case "hap_ip_conns_active":
    case "hap_ip_conns_max":
      el(key).style.display = "inline";
      el(key).innerText = value + " " + key.split("_").slice(-1)[0];
      break;
    case "debug_en":
      el("debug_en").checked = value;
      el("debug_link").style.visibility = value ? "visible" : "hidden";
      break;
    case "rsh_avail":
    case "gdo_avail":
      if (!value) break;
      if (key === "rsh_avail" && !value && el("sys_mode_1")) el("sys_mode_1").remove();
      if (key === "gdo_avail" && !value && el("sys_mode_2")) el("sys_mode_2").remove();
      (el("sys_mode_" + data.sys_mode) || {}).selected = true;
      el("sys_mode_container").style.display = "block";
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
      el("notify_overheat").style.display = (value ? "block" : "none");
      break;
    default:
      console.log(key, value);
  }
}

function getInfo() {
  return new Promise(function (resolve, reject) {
    if (socket.readyState !== 1) {
      reject();
      return;
    }

    sendMessageWebSocket("Shelly.GetInfo").then(function (res) {
      var data = res.result;

      if (data == null) {
        reject();
        return;
      }

      if (data.failsafe_mode) {
        el("notify_failsafe").style.display = "inline";
        resolve();
        return;
      }

      for (let element in data) {
        if (lastInfo == null || (lastInfo[element] !== data[element])) {
          updateElement(element, data[element]);
        }
      }

      lastInfo = data;
      autoRefresh = true;

      el("homekit_container").style.display = "block";
      el("gs_container").style.display = "block";
      el("debug_log_container").style.display = "block";
    }).catch(function (err) {
      alert(err);
      console.log(err);
      reject(err);
    }).then(function () {
      el("spinner").className = "";
      resolve();
    });
  });
}

function getCookie(key) {
  var parts1 = document.cookie.split(";");
  for (var i in parts1) {
    var parts2 = parts1[i].split("=");
    if (parts2[0].trim() == key) return JSON.parse(parts2[1].trim());
  }
  return null;
}

function setCookie(key, value) {
  document.cookie = (key + "=" + JSON.stringify(value));
}

el("debug_en").onclick = function () {
  var debugEn = el("debug_en").checked;
  sendMessageWebSocket("Shelly.SetConfig", {config: {debug_en: debugEn}})
    .then(function (res) {
      getInfo();
    }).catch(function (err) {
    if (err.response) {
      err = err.response.data.message;
    }
    alert(err);
  });
};

var connectionTries = 0;

function connectWebSocket() {
  return new Promise(function (resolve, reject) {
    socket = new WebSocket("ws://" + location.host + "/rpc");
    connectionTries += 1;

    socket.onclose = function(event) {
      console.log("[close] Connection died (code " + event.code + ")");
      el("notify_disconnected").style.display = "inline"
      // attempt to reconnect
      setTimeout(function () {
        connectWebSocket()
          // reload the page once we reconnect (the web ui could have changed)
          .then(() => location.reload())
          .catch(() => console.log("[error] Could not reconnect to Shelly"));
      }, Math.min(3000, connectionTries * 1000));
    };

    socket.onerror = function(error) {
      el("notify_disconnected").style.display = "inline"
      reject(error);
    };

    socket.onopen = function () {
      console.log("[open] Connection established");
      el("notify_disconnected").style.display = "none";
      connectionTries = 0;
      resolve(socket);
    };
  });
}

function sendMessageWebSocket(method, params = [], id = 0) {
  return new Promise(function (resolve, reject) {
    try {
      socket.send(JSON.stringify({"method": method, "id": id, "params": params}));
      console.log("[send] Data sent: " + JSON.stringify({"method": method, "id": id, "params": params}));
    } catch (e) {
      reject(e);
    }

    socket.onmessage = function (event) {
      console.log("[message] Data received: " + event.data);
      resolve(JSON.parse(event.data));
    }

    socket.onerror = function(error) {
      reject(error);
    }
  });
}

el("refresh_btn").style.display = autoRefresh ? "none" : "inline";
el("refresh_btn").onclick = function () {
  el("spinner").className = "spin";
  getInfo();
}

function onLoad() {
  connectWebSocket().then(() => {
    getInfo().then(() => {
      // check for update only once when loading the page (not each time in getInfo)
      if (lastInfo.wifi_rssi !== 0) {
        var last_update_check = parseInt(getCookie("last_update_check"));
        var now = new Date();
        console.log("Last update check:", last_update_check, new Date(last_update_check));
        if (isNaN(last_update_check) || now.getTime() - last_update_check > 24 * 60 * 60 * 1000) {
          checkUpdate();
        }
        el("notify_update").style.display = (getCookie("update_available") ? "block" : "none");
      }
    });
  });

  setInterval(function () {
    // if the socket is open and connected and the page is visible to the user
    if (autoRefresh && socket.readyState === 1 && !document.hidden) getInfo();
  }, 1000);
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
    status.innerText = "Error downloading: " + error;
  });
}

async function uploadFW(blob, spinner, status) {
  spinner.className = "spin";
  status.innerText = "Uploading " + blob.size + " bytes...";
  var fd = new FormData();
  fd.append("file", blob);
  fetch("/update", {
    method: "POST",
    mode: "cors",
    body: fd,
  })
    .then(async (resp) => {
      var respText = await resp.text();
      console.log("resp", resp, respText);
      spinner.className = "";
      status.innerText = respText;
      setCookie("update_available", false);
    }).catch((error) => {
    spinner.className = "";
    status.innerText = "Error uploading: " + error;
  });
}

// major.minor.patch-variantN
function parseVersion(vs) {
  var pp = vs.split("-");
  var v = pp[0].split(".");
  var variant = "", varSeq = 0;
  if (pp[1]) {
    var i = 0;
    for (i in pp[1]) {
      var c = pp[1][i];
      if (isNaN(parseInt(c))) {
        variant += c;
      } else {
        break;
      }
    }
    varSeq = parseInt(pp[1].substring(i)) || 0;
  }
  return {
    major: parseInt(v[0]),
    minor: parseInt(v[1]),
    patch: parseInt(v[2]),
    variant: variant,
    varSeq: varSeq,
  }
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

async function checkUpdate() {
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
      setCookie("last_update_check", (new Date()).getTime());

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
      el("notify_update").style.display = (updateAvailable ? "block" : "none");

      setCookie("update_available", updateAvailable);
      if (!updateAvailable) {
        e.innerText = "Up to date";
        se.className = "";
        return;
      }
      se.className = "";
      e.innerHTML = "Version " + latestVersion + " is available. " +
        'See <a href="' + relNotesURL + '" target="_blank">release notes</a>.';
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
  el("revert_msg").style.display = "block";
  var stockURL = "https://rojer.me/files/shelly/stock/" + lastInfo.stock_model + ".zip";
  downloadUpdate(stockURL, el("revert_btn_spinner"), el("revert_status"));
};
