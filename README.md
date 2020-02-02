# Apple HomeKit firmware for Shelly switches

This firmware exposes Shelly1 and Shelly25 as Apple HomeKi accessories.

Firmware is compatible with stock and can be uploaded via OTA.

Reverting to stock firmware is also supported (see below).

At the moment only switch functionality is supported - no scheduling, power measurement, etc.

## Updating from stock firmware

 * Download one of the release builds (see Releases) or build a firmware package yourself (see Building below).

 * Upload to a plain HTTP (not HTTPS) server. Use local server, e.g. Mongoose binary.

 * Send HTTP request to current device IP with the address of the firmware, e.g. navigate your browser to

`http://192.168.11.75/ota?url=http://192.168.11.30:8080/build/fw.zip`

  `192.168.11.75` is the Shelly device IP, `192.168.11.30:8080` is an HTTP server on local machine.

  * For the impatient, I have latest build of the firmware uploaded to my server:

    * For Shelly1: `http://rojer.me/files/shelly/shelly-homekit-Shelly1.zip`

    * For Shelly25: `http://rojer.me/files/shelly/shelly-homekit-Shelly25.zip`

 * If everything goes well, after 30 seconds or so you will see led blinking (see LED indication section below)
   and a different web interface at the same device address.

## Setup

 * First, set a HAP setup code. Pick any code.

   * Some obvious combinations like 111-11-111 are explicitly disallowed by Apple and will not be accepted.

   * Note that code is not stored on the device in plain text and it is not possible what the current setting is.

 * Provision WiFi station (if not yet).

 * You should see `Shelly1` switch accessory in the list of available accessories and be able to add it with the setup code you entered earlier.

 * Enjoy!

## Recovery

 Device can be recovered from invalid wifi configuration with one of two methods:

  * On Shelly2.5 press and hold the button for 10 seconds.
  * On both models within first 60 seconds of boot, toggle input switch 10 times in succession.

 Both of these methods will make device go int AP mode where they can be reconfigured.

 If the device does not appear in the list of accessories when adding, try resetting the HomeKit status from the web interface.

## LED indication

 Shelly2.5 has an LED that is used to indicate current status of the device

 * Off - fully provisioned, connected to WiFi, paired.
 * Off, short on pulses - HAP server not provisioned (code not set).
 * On, short off pulses - HAP server provisioned, WiFi not provisioned (AP active).
 * Slow blinking (500 ms) - HAP server running, WiFi provisioned, not paired.
 * Fast blinking (250 ms) - connecting to WiFi or performing firmware update.
 * Rapid blinking (100 ms) - HAP accessory identification routine, 3 seconds.

## Reverting to stock firmware

 It is possible to revert back to stock firmware.

 Stock firmware for Shelly1 can be downloaded [here](http://api.shelly.cloud/firmware/SHSW-1_build.zip),
 for Shelly2.5 it's [here](http://api.shelly.cloud/firmware/SHSW-25_build.zip).

 Download it and upload via web interface (this firmware does not support pulling from a remote URL).

## Building

When building, specify `MODEL=Shelly1` or `MODEL=Shelly25`

```
mos build --verbose --local --platform esp8266 --build-var MODEL=Shelly1
```

## Contributions and development

Contributions are welcome!

### OTA method

For development, OTA method is recommended.

Firmware suports OTA via both RPC and HTTP POST, so something like

```
 $ make Shelly25 && curl -F commit_timeout=120 -F file=@build_Shelly25/fw.zip http://192.168.11.75/update
```

Note the use of commit timeout: if something goes wrong (as it invariably does during development),
device will revert to previous firmware automatically. If you are happy with the result, you have 2 minutes
to commit the firmware via `curl http://192.168.11.75/update/commit` or `mos --port=ws://192.168.11.75/rpc call OTA.Commit`.

### UDP logging

To get remote access to logs, configure UDP logging:

```
 $ mos --port=ws://192.168.11.75/rpc config-set debug.udp_log_addr=192.168.11.30:1234
```

192.168.11.30 is the address of your workstation (or any address, really - even external).

Then use any UDP listener such as netcat to catch the logs. It is also integrated into mos console:

```
 $ mos --port udp://:1234/ console
Listening on UDP port 1234...
[Feb  2 03:45:27.030] shellyswitch25-B955B6 59 18.558 2|shelly_main.c:248       Tick uptime: 18.55, RAM: 32880, 22264 free
[Feb  2 03:45:28.058] shellyswitch25-B955B6 60 19.558 2|shelly_main.c:248       Tock uptime: 19.55, RAM: 32880, 22264 free
...
```


## License

This firmware is distributed under Apache 2.0 license.

