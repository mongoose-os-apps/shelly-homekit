# Flashing via serial connection

Shelly1 and Shelly2.5 expose programming pins of ESP8266 that can be used to flash firmware.

## mos utility

Firmware is mased on Mongoose OS, please download and install the mos utility from [here](https://mongoose-os.com/docs/mongoose-os/quickstart/setup.md#1-download-and-install-mos-tool).


## Wiring

 * Pinouts: [Shelly1](https://shelly.cloud/wp-content/uploads/2018/08/shelly1_pinout-800x433.jpg) [Shelly2.5](https://shelly.cloud/wp-content/uploads/2019/01/pin_out-650x397.png).

   * Essentially the same, except Shelly2.5 also exposes RST pin

 * You will need a USB-to-serial converter, any dongle will do, as long as it has RX and TX pins and uses 3.3V TTL level. We will use one based on FT232 chip.

 * *Important:* Make sure your Shelly is unplugged from the mains. It will be powered by USB during flashing.
   Never connect serial connection and mains at the same time! Flash, disconnect serial, then turn on mains.

 * Making the connections
   * Shelly1 uses a standard 2.5mm pitch connector and you can use standard jumper wires.
   * Shelly2.5 uses smaller pitch connector, you will need to buy one or improvise.
   * Either way, you need to connect:
     * RX    - TX
     * TX    - RX
     * GND   - GND
     * 3V3   - 3V3
     * GPIO0 - GND
       * This connection makes ESP8266 boot into flash loader at power up.

   * See [this photo](Shelly1-serial.jpg)
     * Blue wire is the GPIO0 pin and is connected to an extra GND pin on the converter.

## Checking connectivity

 * Connect USB-to-serial converter

 * To test connectivity, dump existing firmware

```
$ mos flash-read --platform esp8266 shelly.bin
Using port /dev/ttyUSB0
Opening /dev/ttyUSB0 @ 115200...
Connecting to ESP8266 ROM, attempt 1 of 10...
  Connected, chip: ESP8266EX
Running flasher @ 921600...
  Flasher is running
Reading 2097152 @ 0x0...
Read 2097152 bytes in 158.02 seconds (103.68 KBit/sec)
Wrote shelly.bin
```

This should produce a 2MB `shelly.bin` file. This file contains full dump of entire flash, including all the settings.

If instead you get this:

```
$ mos flash-read --platform esp8266 shelly.bin
Using port /dev/ttyUSB0
Opening /dev/ttyUSB0 @ 115200...
Connecting to ESP8266 ROM, attempt 1 of 10...
Connecting to ESP8266 ROM, attempt 2 of 10...
Connecting to ESP8266 ROM, attempt 3 of 10...
Connecting to ESP8266 ROM, attempt 4 of 10...
Connecting to ESP8266 ROM, attempt 5 of 10...
Connecting to ESP8266 ROM, attempt 6 of 10...
Connecting to ESP8266 ROM, attempt 7 of 10...
Connecting to ESP8266 ROM, attempt 8 of 10...
Connecting to ESP8266 ROM, attempt 9 of 10...
Connecting to ESP8266 ROM, attempt 10 of 10...
Error: /build/mos-latest-NkaX0W/mos-latest-202002020245+18c1939~bionic0/go/src/github.com/mongoose-os/mos/cli/flash/esp/rom_client/rom_client.go:172: failed to connect to ESP8266 ROM
...
```
Check your connections: GPIO0 must be connected to GND at the time of power up.

Also verify that correct port is being used - mos will use first detected serial port which may not be the right one. Run `mos ports` to get a list and specify the one to use by adding `--port PORT` to the command.

Once you've been able to read existing flash contents, continue one.

## Flashing the HomeKit firmware

 * Power-cycle the device. It must be reset (again, with GPIO0 grounded) or next step will not work.

 * Flash the firmware:
   * If you had to use different port, add `--port PORT`
   * For Shelly2.5 use `http://rojer.me/files/shelly/shelly-homekit-Shelly25.zip` instead.

```
$ mos flash http://rojer.me/files/shelly/shelly-homekit-Shelly1.zip
Fetching http://rojer.me/files/shelly/shelly-homekit-Shelly1.zip...
  done, 966608 bytes.
Loaded switch1/esp8266 version 1.1.0 (20200206-224535/1.1.0-g3fa6854-master)
Using port /dev/ttyUSB0
Opening /dev/ttyUSB0 @ 115200...
Connecting to ESP8266 ROM, attempt 1 of 10...
  Connected, chip: ESP8266EX
Running flasher @ 921600...
  Flasher is running
Flash size: 2097152, params: 0x023f (dio,16m,80m)
Deduping...
   699920 @ 0x8000 -> 577040
   262144 @ 0xbb000 -> 32768
      128 @ 0x1fc000 -> 0
Writing...
     4096 @ 0x0
...
     4096 @ 0x1fb000
Wrote 620320 bytes in 6.51 seconds (744.47 KBit/sec)
Verifying...
...
Booting firmware...
All done!
```

Note that despite what the tool says, firmware has not booted.

 * Disconnect the serial connection and power the device from mains as normal.

 * You should see WiFi AP `shelly1-XXX` or `shellyswitch25-XXX`.

 * Connect, navigate to [http://192.168.33.1/](http://192.168.33.1/) and proceed with configuration.

## Restoring flash contents from backup

 * Set up serial connection as explained above. Boot with GPIO0 grounded.

 * Use `mos flash-write` for perform full reflashing:

```
$ mos flash-write --platform esp8266 0 shelly.bin
Using port /dev/ttyUSB0
Opening /dev/ttyUSB0 @ 115200...
Connecting to ESP8266 ROM, attempt 1 of 10...
  Connected, chip: ESP8266EX
Running flasher @ 921600...
  Flasher is running
Flash size: 2097152, params: 0x023f (dio,16m,80m)
Deduping...
  2097152 @ 0x0 -> 827392
Writing...
     8192 @ 0x0
...
Wrote 827392 bytes in 7.98 seconds (809.59 KBit/sec)
Verifying...
  2097152 @ 0x0
Booting firmware...
```

 * Disconnect, power-cycle - firmware should have been fully restored.
