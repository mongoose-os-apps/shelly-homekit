# Apple HomeKit firmware for Shelly switches

This firmware exposes Shelly1 and Shelly25 as Apple HomeKi accessories.

Firmware is compatible with stock and can be uploaded via OTA.

Reverting to stock firmware is also supported (see below).

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

## Reset


## LED indication

 Shelly25 has an LED that is used to indicate current status of the device

 * Off - fully provisioned, connected to WiFi, paired.
 * Off, short on pulses - HAP server not provisioned (code not set).
 * On, short off pulses - HAP server provisioned, WiFi not provisioned (AP active).
 * Slow blinking (500 ms) - HAP server running, WiFi provisioned, not paired.
 * Fast blinking (250 ms) - connecting to WiFi or performing firmware update.
 * Rapid blinking (100 ms) - HAP accessory identification routine, 3 seconds.

## Reverting to stock firmware

 It is possible to revert back to stock firmware.

 TODO(rojer): document.

## Building
 * Build and flash the firmware

   * When building, specify `MODEL=Shelly1` or `MODEL=Shelly25`

```
mos build --verbose --local --platform esp8266 --build-var MODEL=Shelly1
```

