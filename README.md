# Apple HomeKit firmware for Shelly switches

This firmware exposes Shelly1, Shelly1PM, ShellyPlug-S, Shelly2 and Shelly25 as Apple HomeKit accessories.

Firmware is compatible with stock and can be uploaded via OTA (see below).

Reverting to stock firmware is also supported (see documentation).

*Note:* At the moment only switch functionality is supported - no scheduling, etc.

*Note 2:* Only HomeKit is supported, no Shelly Cloud, MQTT for now.

## Quick Start

### Updating from stock firmware

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
