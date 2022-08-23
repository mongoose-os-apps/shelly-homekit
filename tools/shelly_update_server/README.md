# Shelly-HomeKit Update Server

## Overview

This server is intended as a single entry point for all update requests and will contain
the logic for serving the right update based on what the device is currently running.

Initial version will handle stock -> HomeKit upgrades but eventually HK update logic will be moved here as well,
replacing the current JSON + version regex matching done in UI.

## Running

`go build -v && ./shelly_update_server --listen-addr=:8080 --stock-model-map=stock_model_map.yml`

## Endpoints

 * `/update` - this is the main entry point for all device updates, wille xamine headers and will redirect to the actual ZIP file to update from (according to model and location template from `--dest-url-template`.
 * `/log` - will show recent history of requests from this address, for debugging.
