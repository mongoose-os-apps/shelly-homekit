# Development process

## Requirements

 * Ubuntu or other Linux machine (TODO: test and describe a Windows setup).
 * GNU make.
 * cURL, gzip utilities
 * For local builds - Docker engine (Community Edition).
 * `mos` CLI tool - get it [here](https://mongoose-os.com/docs/mongoose-os/quickstart/setup.md).

## Building

In the top level directory, run `make` with the desired target - one of `Shelly1`, `Shelly1PM`, `Shelly2`, `Shelly25` or `ShellyPlugS`.

```
 $ make Shelly25 MOS=/home/rojer/cesanta/mos/mos
gzip -9 -c fs_src/index.html > fs/index.html.gz
gzip -9 -c fs_src/style.css > fs/style.css.gz
/home/rojer/cesanta/mos/mos build --platform=esp8266 --build-var=MODEL=Shelly25  --build-dir=./build_Shelly25 --binary-libs-dir=./binlibs
Connecting to https://mongoose.cloud, user test
Uploading sources (31156 bytes)
Firmware saved to /home/rojer/allterco/q/build_Shelly25/fw.zip
cp ./build_Shelly25/fw.zip shelly-homekit-Shelly25.zip
```

You don't strictly need Docker to build, it's possible to use remote build server, but local builds are usually faster.

```
$ make Shelly25 LOCAL=1
gzip -9 -c fs_src/index.html > fs/index.html.gz
gzip -9 -c fs_src/style.css > fs/style.css.gz
mos build --platform=esp8266 --build-var=MODEL=Shelly25  --local --build-dir=./build_Shelly25 --binary-libs-dir=./binlibs
Fetching libmbedtls-esp8266-noatca.a (latest) from https://github.com/mongoose-os-libs/mbedtls/releases/download/latest/libmbedtls-esp8266-noatca.a...
Fetching libmongoose-esp8266-nossl.a (latest) from https://github.com/mongoose-os-libs/mongoose/releases/download/latest/libmongoose-esp8266-nossl.a...
Fetching libota-common-esp8266.a (latest) from https://github.com/mongoose-os-libs/ota-common/releases/download/latest/libota-common-esp8266.a...
Fetching libota-http-client-esp8266.a (latest) from https://github.com/mongoose-os-libs/ota-http-client/releases/download/latest/libota-http-client-esp8266.a...
Fetching libota-http-server-esp8266.a (latest) from https://github.com/mongoose-os-libs/ota-http-server/releases/download/latest/libota-http-server-esp8266.a...
Fetching librpc-service-ota-esp8266.a (latest) from https://github.com/mongoose-os-libs/rpc-service-ota/releases/download/latest/librpc-service-ota-esp8266.a...
Firmware saved to /home/rojer/allterco/q/build_Shelly25/fw.zip
cp ./build_Shelly25/fw.zip shelly-homekit-Shelly25.zip
```

Note: First build may take a while - it will have to fetch the build image (~1 GB) and library dependencies. Subsequent builds will be fast.

## Console logs

Device logs useful information to console.
Console is available via serial port - if possible, use that:

```
$ mos console
Using port /dev/ttyUSB0
[Jul 14 22:40:19.912] shelly_main.cpp:273     Uptime: 1828.57, RAM: 33824, 22232 free
[Jul 14 22:40:20.912] shelly_main.cpp:273     Uptime: 1829.57, RAM: 33824, 22232 free
[Jul 14 22:40:22.912] shelly_main.cpp:273     Uptime: 1831.57, RAM: 33824, 22232 free
...
```

Alternatively, you can use UDP logging as follows. Here 192.168.11.x.y is IP address of the device
and 192.168.a.b is ip address of your development machine.

```
$ mos --port ws://192.168.x.y/rpc config-set debug.udp_log_addr=192.168.a.b:8910
```

Now run mos console and you should see the logs:

```
$ mos console --port udp://:8910/
Listening on UDP port 8910...
[Jul 14 22:44:36.037] shellyswitch25-B955B6 2135 2084.575 2|shelly_main.cpp:273     Uptime: 2084.57, RAM: 33824, 22228 free
[Jul 14 22:44:36.959] shellyswitch25-B955B6 2136 2085.575 2|shelly_main.cpp:273     Uptime: 2085.57, RAM: 33824, 22228 free
[Jul 14 22:44:37.982] shellyswitch25-B955B6 2137 2086.575 2|shelly_main.cpp:273     Uptime: 2086.57, RAM: 33824, 22228 free
...
```

For obvious reasons logs are only available once device connects to WiFi, so this doesn't give you early init logs.

TODO: integrate [file-logger library](https://github.com/mongoose-os-libs/file-logger).


## Deploying firmware using OTA

While it is possible to use flashing over serial port as described [here](flashing.md), it's less than convenient in practice.

Delayed commit oTA allows for fail-safe development with deployment over OTA. Here's how it works.

```
$ make Shelly25 LOCAL=1 && curl -F commit_timeout=120 -F file=@build_Shelly25/fw.zip http://shellyswitch25-B955B6.local/update
gzip -9 -c fs_src/index.html > fs/index.html.gz
gzip -9 -c fs_src/style.css > fs/style.css.gz
mos build --platform=esp8266 --build-var=MODEL=Shelly25  --local --build-dir=./build_Shelly25 --binary-libs-dir=./binlibs
Firmware saved to /home/rojer/allterco/shelly-homekit/build_Shelly25/fw.zip
cp ./build_Shelly25/fw.zip shelly-homekit-Shelly25.zip
1 Update applied, rebooting
```

`commit_timeout=120` setting means that after deployment firmware remains in uncommitted state and will wait up to 120 seconds for explicit commit.
If commit does not happen, or device reboots for any reason (e.g. crash), firmware automatically reverts to previous version:

```
```

So, if you are happy with the new firmware, you must comit it within 2 minutes after flashing:

```
$ curl http://shellyswitch25-B955B6.local/update/commit
Ok
```

or

```
 $ mos --port ws://shellyswitch25-B955B6.local/rpc call OTA.Commit
null
```

This allows for safe development without a serial connection.
