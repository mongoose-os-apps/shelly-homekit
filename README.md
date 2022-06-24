[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Gitter](https://badges.gitter.im/shelly-homekit/community.svg)](https://gitter.im/shelly-homekit/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)

# Open Source Apple HomeKit Firmware for Shelly Devices

This firmware exposes Shelly devices as Apple HomeKit accessories.

Firmware is compatible with stock and can be uploaded via OTA (Watch a 2 minute [video](https://www.youtube.com/watch?v=BZc-kp4dDRw)), for more info take a look at the flashing wiki [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#updating-from-stock-firmware).

Reverting to stock firmware is also possible [see here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#reverting-to-stock-firmware).

## Supported devices and features

||[1]|[1PM]|[+1]|[+1PM]|[1L]|[Plug]|[PlugS]|2|[2.5]|[i3]|[+i4]|[UNI]|[RGBW2]|[Bulb]|[Duo]|[Vintage]|
|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|
|Switch & Co.<sup>1</sup>|✓|✓|✓|✓|✓|✓|✓|✓|✓|✗|✗|✓|✗|✗|✗|✗|
|Stateless Input<sup>2</sup>|✓|✓|✓|✓|✓|✗|✗|✓|✓|✓|✓|✓|✓|✗|✗|✗|
|Sensors<sup>3</sup>|✓|✓|✓|✓|✓|✗|✗|✓|✓|✓|✓|✓|✓|✗|✗|✗|
|Temperature measurement|✓<sup>4</sup>|✓<sup>4</sup>|✗|✗|✗|✗|✗|✗|✗|✗|✗|-|✗|✗|✗|✗|
|Garage door opener|✓|✓|✓|✓|✗|✗|✗|✓|✓|✗|✗|✓|✗|✗|✗|✗|
|Roller shutter mode|✗|✗|✗|✗|✗|✗|✗|✗|✓|✗|✗|✗|✗|✗|✗|✗|
|Power measurement|✗|✓|✗|✓|-|✓|✓|✗|✓|✗|✗|✗|-|-|-|-|
|RGB(W)|✗|✗|✗|✗|✗|✗|✗|✗|✗|✗|✗|✗|✓|✓|✗|✗|
|CCT|✗|✗|✗|✗|✗|✗|✗|✗|✗|✗|✗|-|✓|✗|✓|✗|
|Brightness control|✗|✗|✗|✗|✗|✗|✗|✗|✗|✗|✗|-|✓|✓|✓|✓|

_Notes:_  
_✓: supported_  
_-: possible but not supported yet_  
_✗: not possible_  
_1: includes lock, outlet and valve_  
_2: includes doorbell_  
_3: includes motion, occupancy, contact, smoke, leak_  
_4: with [Sensor AddOn](https://shop.shelly.cloud/temperature-sensor-addon-for-shelly-1-1pm-wifi-smart-home-automation#312)_ and DS18B20 sensor  

Features that are not yet supported:
 * Cloud connections: no Shelly Cloud, no MQTT
 * Remote actions (web hooks)
 * Valve with timer support

## Quick Start

### Updating from stock firmware

  * **Important:** Please update to the latest stock firmware prior to converting to Shelly-HomeKit.

  * Watch a 2 minute [video](https://www.youtube.com/watch?v=BZc-kp4dDRw).

    * Shelly 1: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly1.zip`

    * Shelly 1L: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly1L.zip`

    * Shelly 1PM: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly1PM.zip`

    * Shelly 2: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly2.zip`  
      _Note: Not for Shelly Dimmer 2!_

    * Shelly 2.5: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-Shelly25.zip`

    * Shelly Duo: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyDuo.zip`

    * Shelly Duo RGBW (ColorBulb): `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyColorBulb.zip`

    * Shelly i3: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyI3.zip`

    * Shelly Plug: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyPlug.zip`

    * Shelly Plug S: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyPlugS.zip`

    * Shelly Plus 1: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyPlus1.zip`
      _Note: The Shelly must have installed 0.10.0-beta3 or above to be flushed, please update first!_

    * Shelly Plus 1PM: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyPlus1PM.zip`
      _Note: The Shelly must have installed 0.10.0-beta3 or above to be flushed, please update first!_

    * Shelly Plus I4: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyPlusI4.zip`
      _Note: The Shelly must have installed 0.10.0-beta3 or above to be flushed, please update first!_

    * Shelly RGBW2: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyRGBW2.zip`  
      _Note: The Shelly must be in color mode to flash, flashing in white mode is not supported!_

    * Shelly UNI: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyUNI.zip`

    * Shelly Vintage: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyVintage.zip`

  * See [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#updating-from-stock-firmware) for detailed instructions.

  * Script [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#Script) for an automated way to update your devices.
    * ./flash_shelly.py hostname  (for single device)
    * ./flash_shelly.py -a  (for all devices on the network)

## Documentation

See our [Wiki](https://github.com/mongoose-os-apps/shelly-homekit/wiki).

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

[1]: https://shelly.cloud/products/shelly-1-smart-home-automation-relay/
[1PM]: https://shelly.cloud/products/shelly-1pm-smart-home-automation-relay/
[+1]: https://shelly.cloud/shelly-plus-1/
[+1PM]: https://shelly.cloud/shelly-plus-1pm/
[1L]: https://shelly.cloud/products/shelly-1l-single-wire-smart-home-automation-relay/
[Plug]: https://shelly.cloud/products/shelly-plug-smart-home-automation-device/
[PlugS]: https://shelly.cloud/products/shelly-plug-s-smart-home-automation-device/
[2.5]: https://shelly.cloud/products/shelly-25-smart-home-automation-relay/
[i3]: https://shelly.cloud/products/shelly-i3-smart-home-automation-device/
[+i4]: https://shelly.cloud/shelly-plus-i4/
[UNI]: https://shelly.cloud/products/shelly-uni-smart-home-automation-device/
[RGBW2]: https://shelly.cloud/products/shelly-rgbw2-smart-home-automation-led-controller/
[Bulb]: https://shelly.cloud/products/shelly-bulb-smart-home-automation-device/
[Duo]: https://shelly.cloud/products/shelly-duo-smart-home-automation-bulb/
[Vintage]: https://shelly.cloud/products/shelly-vintage-smart-home-automation-bulb/
