[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Gitter](https://badges.gitter.im/shelly-homekit/community.svg)](https://gitter.im/shelly-homekit/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)

# Apple HomeKit firmware for Shelly switches

This firmware exposes Shelly1, Shelly1PM, ShellyPlug-S, Shelly2 and Shelly2.5 as Apple HomeKit accessories.

Firmware is compatible with stock and can be uploaded via OTA (Watch a 2 minute [video](https://www.youtube.com/watch?v=BZc-kp4dDRw)), for more info take a look at the flashing wiki [here](Flashing#updating-from-stock-firmware).

At the moment only the following functionality is supported:
 * Switch functionality - all relevant models, with auto-off and separate input tile as an option.
 * Power measurement: only Shelly 2.5, not Shelly1PM
 * Roller-shutter mode (Shelly 2.5 only)

Reverting to stock firmware is also supported [see here](Flashing#reverting-to-stock-firmware).

*Note:* Only HomeKit is supported, no Shelly Cloud, MQTT or web hooks for now.

## Quick Start

### Updating from stock firmware

  * **Important:** Please update to the latest stock firmware prior to converting to Shelly-HomeKit.

  * Watch a 2 minute [video](https://www.youtube.com/watch?v=BZc-kp4dDRw).

    * Shelly 1: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly1.zip`

    * Shelly 1PM: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly1PM.zip`

    * Shelly 2: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly2.zip`

    * Shelly 2.5: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly25.zip`

    * Shelly Plug S: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyPlugS.zip`

  * See [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#updating-from-stock-firmware) for detailed instructions.

  * Script [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#Script) for an automated way to update your devices.
    * ./flash_shelly.sh hostname  (for single device)
    * ./flash_shelly.sh -a  (for all devices on the network)

## Documentation

See [Wiki](https://github.com/mongoose-os-apps/shelly-homekit/wiki).

## Contributions and Development

Code contributions are welcome! See open issues and feel free to pick one up.

See [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Development) for development environment setup.

Alternatively, you can support the project by donating:

[![Donate via PayPal](https://www.paypalobjects.com/en_US/i/btn/btn_donate_SM.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=6KPSKWJDHVLB4)

## Authors

See [here](AUTHORS.md).

## License

This firmware is free software and is distributed under [Apache 2.0 license](LICENSE).
