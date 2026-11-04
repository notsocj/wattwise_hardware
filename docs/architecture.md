# Hardware Architecture

## Data path

```text
PZEM-004T v3 -> ESP32-S3 (Serial2) -> Wi-Fi -> Supabase REST (Seoul)
                                      |              |
                                      v              v
                               local WebServer   energy_logs INSERT
                                      |              |  triggers: smart budget,
                                      v              |  devices.last_seen_at
                                  relay GPIO 15      v
                                      ^          Supabase Realtime
                                      |              |
                  relay_state poll (nominal 5 s) <- wattwise_platform web app
```

## Hardware mapping

| Function | Pin / value |
| --- | --- |
| PZEM RX (ESP32 receives from PZEM TX) | GPIO 4 |
| PZEM TX (ESP32 transmits to PZEM RX) | GPIO 5 |
| Relay output | GPIO 15 |
| BOOT/reset input | GPIO 0, `INPUT_PULLUP` |
| PZEM Modbus address | `0x01` |

The relay is configured for a high-trigger jumper: `HIGH` means ON and `LOW` means OFF. Startup explicitly drives it LOW. Holding BOOT for more than five seconds clears Wi-FiManager credentials and restarts the board.

## Timing and aggregation

Sensor reads are scheduled every two seconds and collect voltage, current, power, and cumulative energy from the PZEM. Valid readings update the local cache and add power to an in-memory sum. Cloud sync is scheduled every five seconds while Wi-Fi is connected. Each cycle polls the relay state if registration has been verified, then attempts to post one averaged-power telemetry row if uploads are not backing off. A successful post clears the in-memory sum and count. Failed uploads back off (10 s doubling to 120 s); that backoff no longer suppresses relay polls. HTTP requests are synchronous with an 8-second timeout, however, so a slow or timed-out request can delay the main loop, including sensor reads and the next relay poll.

The local HTTP server listens on port 80 and serves `/`, refreshing a basic live telemetry page every two seconds. It exposes the device MAC and the cached electrical readings.

## How the platform uses this timing

- A unit shows as LIVE on the dashboard, and online in the admin pages, only if its latest reading is under 20 seconds old (four missed 5-second syncs). Slowing the cloud sync past 5 s makes healthy units flicker offline unless `ACTIVE_READING_WINDOW_MS` in `wattwise_platform/lib/device-telemetry.ts` changes with it.
- The dashboard updates watts, volts, and amps from each inserted row through Supabase Realtime, without reloading the page.
- A relay change made in the app (toggle, Restore Power, or budget cutoff) is written to `devices.relay_state` and applied by the unit on its next successful relay poll. Five seconds is the nominal polling interval, not a maximum physical-response time; slow HTTP requests and connectivity failures can delay the command.
