# Hardware Architecture

## Data path

```text
PZEM-004T v3 -> ESP32-S3 (Serial2) -> Wi-Fi -> Supabase REST
                                      |              |
                                      v              v
                               local WebServer   wattwise_platform
                                      |          energy_logs + devices
                                      v
                                  relay GPIO 15
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
