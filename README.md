[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Gitter](https://badges.gitter.im/shelly-homekit/community.svg)](https://gitter.im/shelly-homekit/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)

# Open Source Apple HomeKit Firmware for Shelly Devices

This firmware exposes Shelly devices as Apple HomeKit accessories.

Firmware is compatible with stock and can be uploaded via OTA (Watch a 2 minute [video](https://www.youtube.com/watch?v=BZc-kp4dDRw)), for more info take a look at the flashing wiki [here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#updating-from-stock-firmware).

Reverting to stock firmware is also possible [see here](https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#reverting-to-stock-firmware).

## Supported devices and features

### Plus devices

||[+1]|[+1PM]|+i4 [AC]/[DC]|
|-|-|-|-|
|Switch & Co.<sup>1</sup>|✓|✓|✗|
|Stateless Input<sup>2</sup>|✓|✓|✓|
|Sensors<sup>3</sup>|✓|✓|✓|
|Garage door opener|✓|✓|✗|
|Roller shutter mode|✗|✗|✗|
|Power measurement|✗|✓|✗|
|Temperature/Humidity measurement<sup>4</sup>|✓|✓|✓|

### Pro devices

Currently not supported.

### Gen 1 switches

||[1]|1PM|[1L]|[Plug]|[PlugS]|2|2.5|i3|[UNI]|
|-|-|-|-|-|-|-|-|-|-|
|Switch & Co.<sup>1</sup>|✓|✓|✓|✓|✓|✓|✓|✗|✓|
|Stateless Input<sup>2</sup>|✓|✓|✓|✗|✗|✓|✓|✓|✓|
|Sensors<sup>3</sup>|✓|✓|✓|✗|✗|✓|✓|✓|✓|
|Temperature/Humidity measurement|✓<sup>4</sup>|✓<sup>4</sup>|✗|✗|✗|✗|✗|✗|-|
|Garage door opener|✓|✓|✗|✗|✗|✓|✓|✗|✓|
|Roller shutter mode|✗|✗|✗|✗|✗|✗|✓|✗|✗|
|Power measurement|✗|✓|-|✓|✓|✗|✓|✗|✗|

### Gen 1 light bulbs / led strips

||[Duo]|[Duo RGBW]|[Vintage]|[RGBW2]|
|-|-|-|-|-|
|Brightness control|✓|✓|✓|✓|
|CCT|✓|✗|✗|✓|
|RGB(W)|✗|✓|✗|✓|
|Power measurement|-|-|-|-|

_Notes:_  
_✓: supported_  
_-: possible but not supported yet_  
_✗: not possible_  
_1: includes lock, outlet and valve_  
_2: includes doorbell_  
_3: includes motion, occupancy, contact, smoke, leak_  
_4: with [Sensor AddOn/Shelly Plus AddOn](https://shop.shelly.cloud/temperature-sensor-addon-for-shelly-1-1pm-wifi-smart-home-automation#312)_ and DS18B20 sensor(s) or DHT sensor

Features that are not yet supported:
 * Cloud connections: no Shelly Cloud, no MQTT
 * Remote actions (web hooks)
 * Valve with timer support

## Quick Start

### Updating from stock firmware

  * **Important:** Please update to the latest stock firmware prior to converting to Shelly-HomeKit.

  * Watch a 2 minute [video](https://www.youtube.com/watch?v=BZc-kp4dDRw).

    * *New:* One link for all device types: `http://A.B.C.D/ota?url=http://shelly.rojer.cloud/update`
    <details>
      <summary>If that doesn't work (did you remember to update the stock firmware first?), try link for a specific model</summary>
  
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

      * Shelly Plus I4 AC & DC: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyPlusI4.zip`
        _Note: The Shelly must have installed 0.10.0-beta3 or above to be flushed, please update first!_

      * Shelly RGBW2: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyRGBW2.zip`  
        _Note: The Shelly must be in color mode to flash, flashing in white mode is not supported!_

      * Shelly UNI: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyUNI.zip`

      * Shelly Vintage: `http://A.B.C.D/ota?url=http://rojer.me/files/shelly/shelly-homekit-ShellyVintage.zip`
     </details>

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

[1]: https://www.shelly.cloud/en/products/shop/1xs1
[+1]: https://www.shelly.cloud/en/products/shop/shelly-plus-1
[+1PM]: https://www.shelly.cloud/en/products/shop/shelly-plus-1-pm-2-pack/shelly-plus-1-pm
[1L]: https://www.shelly.cloud/en/products/shop/shelly-1l
[Plug]: https://www.shelly.cloud/en/products/shop/1xplug
[PlugS]: https://www.shelly.cloud/en/products/shop/shelly-plug-s
[AC]: https://www.shelly.cloud/en-de/products/product-overview/splusi4x1
[DC]: https://www.shelly.cloud/en-de/products/product-overview/shelly-plus-i4-dc
[UNI]: https://www.shelly.cloud/en/products/shop/shelly-uni-1
[RGBW2]: https://www.shelly.cloud/en/products/shop/shelly-rgbw2-1
[Duo RGBW]: https://www.shelly.cloud/en/search?query=%22Shelly+Duo+-+RGBW%22
[Duo]: https://www.shelly.cloud/en/search?query=%22Shelly+Duo%22
[Vintage]: https://www.shelly.cloud/en/search?query=vintage
