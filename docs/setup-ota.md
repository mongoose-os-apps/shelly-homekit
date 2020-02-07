# Updating from stock firmware

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
