var lastInfo = null;

var socket = null;

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
  return !!name.match(/^[a-z0-9\-]{1,63}$/i)
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
  pauseAutoRefresh = true;
  sendMessageWebSocket("Shelly.SetConfig", data).then(function () {
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

el("hap_save_btn").onclick = function () {
  var codeMatch = hapSetupCode.value.match(/^(\d{3})\-?(\d{2})\-?(\d{3})$/);
  if (!codeMatch) {
    alert("Invalid code '" + hapSetupCode.value + "', must be xxxyyzzz or xxx-yy-zzz.");
    return;
  }
  var code = codeMatch.slice(1).join('-');
  hapSaveSpinner.className = "spin";
  sendMessageWebSocket("HAP.Setup", {"code": code})
    .catch(function (err) {
      if (err.response) {
        err = err.response.data.message;
      }
      alert(err);
    }).then(function () {
    hapSaveSpinner.className = "";
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
  pauseAutoRefresh = true;
  sendMessageWebSocket("Shelly.SetConfig", data)
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
  console.log("SetState:", data);
  sendMessageWebSocket("Shelly.SetState", data)
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
    days, hours, minutes, seconds, minutes
  } = dateString.match(
    /^(?<days>\d+)\:(?<hours>\d{2})\:(?<minutes>\d{2})\:(?<seconds>\d{2})\:(?<milliseconds>\d{3})/
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

el("reset_btn").onclick = function () {
  sendMessageWebSocket("Shelly.WipeDevice", {}).then(function () {
    alert("Device configuration has been reset, it will reboot in AP mode.");
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
      setValueIfNotModified(el(c, "name"), cd.name);
      el(c, "state").innerText = (cd.state ? "on" : "off");
      if (cd.apower !== undefined) {
        el(c, "power_stats").innerText = ", " + Math.round(cd.apower) + "W, " + cd.aenergy + "Wh";
      }
      el(c, "btn_label").innerText = "Turn " + (cd.state ? "Off" : "On");
      selectIfNotModified(el(c, "svc_type"), cd.svc_type);
      if (cd.svc_type == 3) {
        selectIfNotModified(el(c, "valve_type"), cd.valve_type);
        el(c, "valve_type_container").style.display = "block";
        el(c, "valve_type_label").innerText = "Valve Type:";
      } else {
        el(c, "valve_type_container").style.display = "none";
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
      checkIfNotModified(el(c, "auto_off"), cd.auto_off);
      el(c, "auto_off_delay").disabled = !el(c, "auto_off").checked;
      setValueIfNotModified(el(c, "auto_off_delay"), secondsToDateString(cd.auto_off_delay));
      if (cd.state_led_en == -1) {
        el(c, "state_led_en_container").style.display = "none";
      } else {
        el(c, "state_led_en_container").style.display = "block";
        checkIfNotModified(el(c, "state_led_en"), cd.state_led_en == 1);
      }
      break;
    case 3: // Stateless Programmable Switch (aka input in detached mode).
      var headText = "Input " + cd.id;
      if (cd.name) headText += " (" + cd.name + ")";
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
        lastEvText = lastEv + " (" + secondsToDateString(cd.last_ev_age) + " ago)";
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
      var headText = "Input " + cd.id;
      el(c, "head").innerText = headText;
      selectIfNotModified(el(c, "type"), cd.type);
      break;
    case 7: // Motion Sensor
    case 8: // Occupancy Sensor
    case 9: // Contact Sensor
      var headText = "Input " + cd.id;
      if (cd.name) headText += " (" + cd.name + ")";
      el(c, "head").innerText = headText;
      setValueIfNotModified(el(c, "name"), cd.name);
      selectIfNotModified(el(c, "type"), cd.type);
      checkIfNotModified(el(c, "inverted"), cd.inverted);
      selectIfNotModified(el(c, "in_mode"), cd.in_mode);
      setValueIfNotModified(el(c, "idle_time"), cd.idle_time);
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
      break;
    case "model":
    case "device_id":
    case "version":
    case "fw_build":
      el(key).innerText = value;
      break;
    case "name":
      el("device_name").innerText = document.title = value;
      el("device_name").style.visibility = "visible";
      setValueIfNotModified(el("sys_name"), value);
      break;
    case "wifi_en":
      checkIfNotModified(wifiEn, value);
      break;
    case "wifi_ssid":
      setValueIfNotModified(wifiSSID, value);
      break;
    case "wifi_pass":
      setValueIfNotModified(wifiPass, value);
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
      // the number of components has changed, delete them all and start afresh
      if (lastInfo !== null && lastInfo.components.length !== value.length) el("components").innerHTML = "";
      for (let i in value) updateComponent(value[i]);
      break;
    case "hap_running":
      setValueIfNotModified(hapSetupCode, value ? "***-**-***" : "");
      if (!value) el("hap_ip_conns_max").innerText = "Server not running"
      el("hap_ip_conns_pending").style.display = "none";
      el("hap_ip_conns_active").style.display = "none";
      break;
    case "hap_ip_conns_pending":
    case "hap_ip_conns_active":
    case "hap_ip_conns_max":
      el(key).style.display = "inline";
      el(key).innerText = value + " " + key.split("_").slice(-1)[0];
      break;
    case "debug_en":
      checkIfNotModified(el("debug_en"), value);
      el("debug_link").style.visibility = value ? "visible" : "hidden";
      break;
    case "rsh_avail":
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
    default:
      //console.log(key, value);
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
        // only show this limited set of infos
        ["model", "device_id", "version", "fw_build"]
          .forEach(element => updateElement(element, data[element]));
        reject();
        return;
      }

      // the system mode changed, clear out old UI components
      if (lastInfo !== null && lastInfo.sys_mode !== data.sys_mode) el("components").innerHTML = "";

      for (let element in data) {
        updateElement(element, data[element]);
      }

      lastInfo = data;

      el("homekit_container").style.display = "block";
      el("gs_container").style.display = "block";
      el("debug_log_container").style.display = "block";
    }).catch(function (err) {
      alert(err);
      console.log(err);
      reject(err);
    }).then(resolve);
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
  pauseAutoRefresh = true;
  sendMessageWebSocket("Shelly.SetConfig", {config: {debug_en: debugEn}})
    .then(() => setTimeout(() => {
      pauseAutoRefresh = false;
      refreshUI();
    }, 1300))
    .catch(function (err) {
      if (err.response) {
        err = err.response.data.message;
      }
      alert(err);
      pauseAutoRefresh = false;
  });
};

var connectionTries = 0;

function connectWebSocket() {
  return new Promise(function (resolve, reject) {
    socket = new WebSocket("ws://" + location.host + "/rpc");
    connectionTries += 1;

    socket.onclose = function (event) {
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

    socket.onerror = function (error) {
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

let requestID = 0;

function sendMessageWebSocket(method, params = [], id = requestID) {
  requestID += 1
  return new Promise(function (resolve, reject) {
    try {
      socket.send(JSON.stringify({"method": method, "id": id, "params": params}));
      console.log("[send] Data sent: " + JSON.stringify({"method": method, "id": id, "params": params}));
    } catch (e) {
      reject(e);
    }

    socket.addEventListener("message", function (event) {
      let data = JSON.parse(event.data);
      if (data.id === id) {
        console.log("[message] Data received: ", data);
        resolve(data);
        // clean up after ourselves, otherwise too many listeners!
        socket.removeEventListener("message", arguments.callee);
      }
    });

    socket.onerror = function (error) {
      reject(error);
    }
  });
}


// noinspection JSUnusedGlobalSymbols
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
        el("notify_update").style.display = (getCookie("update_available") ? "inline" : "none");
      }

      // auto-refresh if getInfo resolved (it rejects if in failsafe mode i.e. not auto-refresh)
      setInterval(refreshUI, 1000);

    }).catch(() => {
      console.log("getInfo() rejected; failsafe mode?");
    });
  });
}

var pauseAutoRefresh = false;
var pendingGetInfo = false;

function refreshUI() {
  // if the socket is open and connected and the page is visible to the user
  if (socket.readyState === 1 && !document.hidden && !pauseAutoRefresh && !pendingGetInfo) {
    pendingGetInfo = true;
    getInfo()
      .then(() => pendingGetInfo = false)
      .catch(() => pendingGetInfo = false);
  }
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
      el("notify_update").style.display = (updateAvailable ? "inline" : "none");

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

