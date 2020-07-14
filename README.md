# Apple HomeKit firmware for Shelly switches

This firmware exposes Shelly1, SHelly1PM, ShellyPlug-S, Shelly2 and Shelly25 as Apple HomeKit accessories.

Firmware is compatible with stock and can be uploaded via OTA (see below) or [flashed via serial connection](docs/flashing.md).

Reverting to stock firmware is also supported (see below).

*Note:* At the moment only switch functionality is supported - no scheduling, power measurement, etc.

*Note 2:* Only HomeKit is supported, no Shelly Cloud, MQTT for now.

## Updating from stock firmware

  * Watch a 2 minute [video](https://www.youtube.com/watch?v=BZc-kp4dDRw).

    * Shelly 1: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly1.zip`

    * Shelly 1PM: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly1PM.zip`

    * Shelly 2: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly2.zip`

    * Shelly 2.5: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly25.zip`

    * Shelly Plug S: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyPlugS.zip`

  * See [here](docs/setup-ota.md) for detailed instructions.

## Setup

 * Before device can be added to a Home, setup code needs to be configured (via web interface). Pick any code.

   * Some obvious combinations like 111-11-111 are explicitly disallowed by Apple and will not be accepted.

   * Note that code is not stored on the device in plain text and it is not possible to read what the current setting is.

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

 Shelly2.5 and ShellyPlug-S have an LED that is used to indicate current status of the device

 * Off - fully provisioned, connected to WiFi, paired.
 * Off, short on pulses - HAP server not provisioned (code not set).
 * On, short off pulses - HAP server provisioned, WiFi not provisioned (AP active).
 * Slow blinking (500 ms) - HAP server running, WiFi provisioned, not paired.
 * Fast blinking (250 ms) - connecting to WiFi or performing firmware update.
 * Rapid blinking (100 ms) - HAP accessory identification routine, 3 seconds.

## Reverting to stock firmware

 It is possible to revert back to stock firmware.

 Stock firmware for can be downloaded from the offical site:
  * [Shelly 1](http://api.shelly.cloud/firmware/SHSW-1_build.zip)
  * [Shelly 1PM](http://api.shelly.cloud/firmware/SHSW-PM_build.zip)
  * [Shelly 2](http://api.shelly.cloud/firmware/SHSW-21_build.zip)
  * [Shelly 2.5](http://api.shelly.cloud/firmware/SHSW-25_build.zip)
  * [Shelly Plug S](http://api.shelly.cloud/firmware/SHPLG-S_build.zip)

 Download it and upload via web interface (this firmware does not support pulling from a remote URL).

## Contributions and Development

Code contributions are welcome! See open issues and feel free to pick one up.

See [here](docs/development.md) for development environment setup.

Alternatively, you can support the project by donating:

[![Donate via PayPal](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=6KPSKWJDHVLB4)

## License

This firmware is free software and is distributed under [Apache 2.0 license](LICENSE).
