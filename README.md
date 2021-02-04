[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Gitter](https://badges.gitter.im/shelly-homekit/community.svg)](https://gitter.im/shelly-homekit/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)

# Open Source Apple HomeKit Firmware for Shelly Devices

This firmware exposes Shelly devices as Apple HomeKit accessories.

Firmware is compatible with stock and can be uploaded via OTA (Watch a 2 minute [video](https://www.youtube.com/watch?v=BZc-kp4dDRw)), for more info take a look at the flashing wiki [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#updating-from-stock-firmware).

Reverting to stock firmware is also possible [see here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#reverting-to-stock-firmware).

Summary of supported devices and features:
 * [Shelly 1](https://shelly.cloud/products/shelly-1-smart-home-automation-relay/), [Shelly 1PM](https://shelly.cloud/products/shelly-1pm-smart-home-automation-relay/)
   * Switch/lock/outlet/valve
   * Stateless input
   * Garage door opener mode
 * [Shelly 1L](https://shelly.cloud/products/shelly-1l-single-wire-smart-home-automation-relay/)
   * Switch/lock/outlet/valve
   * Stateless input
 * [Shelly Plug](https://shelly.cloud/products/shelly-plug-smart-home-automation-device/), [Shelly Plug S](https://shelly.cloud/products/shelly-plug-s-smart-home-automation-device/)
   * Switch/lock/outlet
 * Shelly 2
   * Switch/lock/outlet/valve
   * Stateless input
   * Garage door opener mode
 * [Shelly 2.5](https://shelly.cloud/products/shelly-25-smart-home-automation-relay/)
   * Switch/lock/outlet/valve
   * Stateless input
   * Garage door opener mode
   * Roller shutter mode
   * Power measurement
 * [Shelly i3](https://shelly.cloud/products/shelly-i3-smart-home-automation-device/)
   * Stateless input

Features that are not yet supported:
 * Shelly 1L, 1PM, 2, Plug, Plug S: power measurement
 * Cloud connections: no Shelly Cloud, no MQTT
 * Remote actions (web hooks)

## Quick Start

### Updating from stock firmware

  * **Important:** Please update to the latest stock firmware prior to converting to Shelly-HomeKit.

  * Watch a 2 minute [video](https://www.youtube.com/watch?v=BZc-kp4dDRw).

    * Shelly 1: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly1.zip`

    * Shelly 1L: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly1L.zip`

    * Shelly 1PM: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly1PM.zip`

    * Shelly 2: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly2.zip`

    * Shelly 2.5: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly25.zip`

    * Shelly i3: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyI3.zip`

    * Shelly Plug: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyPlug.zip`

    * Shelly Plug S: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyPlugS.zip`

  * See [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#updating-from-stock-firmware) for detailed instructions.

  * Script [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#Script) for an automated way to update your devices.
    * ./flash_shelly.py hostname  (for single device)
    * ./flash_shelly.py -a  (for all devices on the network)

## Documentation

See [Wiki](https://github.com/mongoose-os-apps/shelly-homekit/wiki).

## Getting Support

If you'd like to report a bug or a missing feature, please use [GitHub issue tracker](https://github.com/mongoose-os-apps/shelly-homekit/issues).

Some of us can be found in the [Gitter chat room](https://gitter.im/shelly-homekit/community).

## Contributions and Development

Code contributions are welcome! Check out [open issues](https://github.com/mongoose-os-apps/shelly-homekit/issues) and feel free to pick one up.

See [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Development) for development environment setup.

Alternatively, you can support the project by donating:

[![Donate via PayPal](https://www.paypalobjects.com/en_US/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=6KPSKWJDHVLB4)

## Authors

See [here](AUTHORS.md).

## License

This firmware is free software and is distributed under [Apache 2.0 license](LICENSE).
