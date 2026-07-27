# Operations and Known Limitations

## Provisioning

On first boot, connect to the `WattWise-Setup` Wi-FiManager portal and configure Wi-Fi. The device prints its MAC address over serial; register that exact colon-separated value in `wattwise_platform.devices.mac_address` before expecting telemetry or relay commands.

## Verification checklist

1. Confirm the serial monitor reports a stable Wi-Fi connection and MAC.
2. Confirm the MAC exists in `devices` and the relay GET returns one row.
3. Confirm POST responses are 2xx and new rows appear in `energy_logs`.
4. Confirm the platform dashboard reads the new row and that a relay-state change propagates within five seconds.
5. Test with a safe load before connecting tenant appliances.

## Current limitations

- No offline queue, retry persistence, sequence number, or idempotency key; readings can be lost during outages.
- Telemetry aggregation is acknowledged only for 2xx responses; failed batches remain in memory for bounded retries. A power loss can still discard an in-memory batch.
- Relay state is parsed with ArduinoJson and invalid/empty responses leave the fail-safe relay state unchanged.
- Direct five-second polling creates roughly 34,560 requests per device per day.
- MAC addresses and the anon key are not hardware authentication. Production deployments need device-specific credentials or a controlled ingestion gateway.
- Startup defaults the relay OFF during connectivity failures. Decide whether this is acceptable per appliance type, especially refrigerators.
