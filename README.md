# Apple HomeKit firmware for Shelly switches

## Setup

 * Build and flash the firmware

   * When building, specify `MODEL=Shelly1` or `MODEL=Shelly25`

```
mos build --verbose --local --platform esp8266 --build-var MODEL=Shelly1
```

   * TODO: OTA update from stock FW

 * Provision the HAP accessory and connect to WiFi:

   * Over local serial connection
```
 $ mos call HAP.SetupInfo '{"code": "222-33-444"}'
 $ mos wifi MYNETWORK MYPASSWORD
```

   * While connected to WiFi AP (`Shelly1-XXXX`):
```
 $ mos call --port=ws://192.168.4.1/rpc HAP.SetupInfo '{"code": "222-33-444"}'
 $ mos wifi --port=ws://192.168.4.1/rpc MYNETWORK MYPASSWORD
```

 * You should see `Shelly1` switch accessory in the list of available accessories,
   add it using the specified code (`222-33-444` in this case).

 * Enjoy!
