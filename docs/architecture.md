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
                       relay_state poll (5 s) <- wattwise_platform web app
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

Every two seconds the firmware reads voltage, current, power, and cumulative energy from the PZEM. Valid readings update the local cache and add power to an in-memory sum. Every five seconds, while Wi-Fi is connected, the firmware first polls the relay state and then posts one averaged-power telemetry row. A successful post clears the in-memory sum and count.

The local HTTP server listens on port 80 and serves `/`, refreshing a basic live telemetry page every two seconds. It exposes the device MAC and the cached electrical readings.

## How the platform uses this timing

- A unit shows as LIVE on the dashboard, and online in the admin pages, only if its latest reading is under 20 seconds old (four missed 5-second syncs). Slowing the cloud sync past 5 s makes healthy units flicker offline unless `ACTIVE_READING_WINDOW_MS` in `wattwise_platform/lib/device-telemetry.ts` changes with it.
- The dashboard updates watts, volts, and amps from each inserted row through Supabase Realtime, without reloading the page.
- A relay change made in the app (toggle, Restore Power, or budget cutoff) is written to `devices.relay_state` and applied by the unit on its next 5-second relay poll, so the physical switch can lag the app by up to about 5 seconds.
