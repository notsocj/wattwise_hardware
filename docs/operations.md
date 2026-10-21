# Operations and Known Limitations

## Provisioning

On first boot, connect to the `WattWise-Setup` Wi-FiManager portal and configure Wi-Fi. The device prints its MAC address over serial; register that exact colon-separated value in `wattwise_platform.devices.mac_address` before expecting telemetry or relay commands.

## Verification checklist

1. Confirm the serial monitor reports a stable Wi-Fi connection and MAC.
2. Confirm the MAC exists in `devices` and the relay GET returns one row.
3. Confirm POST responses are 2xx (201) and new rows appear in `energy_logs`.
4. Confirm the dashboard card switches to LIVE within a few seconds and its watts, volts, and amps update on each post.
5. Confirm the admin devices table shows the unit as Online, which means `devices.last_seen_at` is updating. If it stays "never seen" while rows arrive, the device is unpaired (`user_id` is null) or platform migration `036` is missing.
6. Confirm a relay toggle in the app reaches the relay within about five seconds, and that the app asks for confirmation before turning a unit off.
7. Unplug Wi-Fi and confirm the dashboard shows the unit as offline within about 20 seconds.
8. Test with a safe load before connecting tenant appliances.

## Current limitations

- No offline queue, retry persistence, sequence number, or idempotency key; readings can be lost during outages.
- Telemetry aggregation is acknowledged only for 2xx responses; failed batches remain in memory for bounded retries. A power loss can still discard an in-memory batch.
- Relay state is parsed with ArduinoJson and invalid/empty responses leave the fail-safe relay state unchanged.
- Direct five-second polling creates roughly 34,560 requests per device per day and about 17,280 `energy_logs` rows. The platform's reads are index-backed and bounded, but the table has no retention or downsampling yet.
- `devices.is_online` is set on every reading and never cleared, so it does not mean "online now". Use `last_seen_at` freshness, as the platform does.
- MAC addresses and the anon key are not hardware authentication. Production deployments need device-specific credentials or a controlled ingestion gateway.
- Startup defaults the relay OFF during connectivity failures. Decide whether this is acceptable per appliance type, especially refrigerators.
