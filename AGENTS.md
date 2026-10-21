# WattWise Hardware Agent Guide

## Scope

This repository contains the ESP32-S3 firmware for WattWise sub-meter units. The firmware reads a PZEM-004T v3 meter, controls a relay, serves a local telemetry page, and synchronizes with the WattWise Supabase project. `wattwise_platform` is the consumer of the resulting `energy_logs` rows and the source of the database migrations/RLS policies.

The current source of truth is `main`. Keep `wattwise_cloud` out of scope.

## Repository map

- `src/main.cpp`: all firmware behavior, pin mapping, Wi-Fi provisioning, local web page, Supabase GET/POST logic.
- `platformio.ini`: PlatformIO environment, board, framework, and library dependencies.
- `docs/`: hardware architecture, Supabase contract, and operations notes.

## Build and flash

Use PlatformIO with the environment named `esp32coore` (the spelling is part of the existing configuration). The target is `esp32-s3-devkitc-1`, Arduino framework, 115200 baud serial monitor. Do not change pins or relay polarity without checking the physical jumper configuration.

## Runtime contract

- PZEM RX/TX: GPIO 4/5; relay: GPIO 15; BOOT reset button: GPIO 0.
- PZEM is set to Modbus address `0x01` during setup.
- Sensor reads occur every 2 seconds; cloud sync occurs every 5 seconds.
- Device identity is the normalized Wi-Fi MAC address.
- Relay state is read from `devices` with `?select=relay_state&mac_address=eq.<MAC>`.
- Telemetry is inserted into `energy_logs` with `device_id` equal to the MAC and fields `average_watts`, `energy_kwh`, `voltage_v`, and `current_a`.
- `recorded_at` is assigned by the database. Do not send it: `(device_id, recorded_at)` is unique.
- POSTs must keep PostgREST's default `return=minimal` (no `Prefer: return=representation`). The anon role cannot read `energy_logs`, so asking for the inserted row back would fail the request.

## Platform contract (updated October 21, 2026)

These platform behaviors depend on the firmware's timing and identity. Change them only together with `wattwise_platform`.

- **Online means a reading in the last 20 seconds.** The dashboard shows a unit as LIVE, and admin pages count it as online, only if its latest reading (or `devices.last_seen_at`) is under 20 s old, which is four missed 5-second syncs. `devices.is_online` is never reset and is not used for status. Keep the cloud sync interval at or below 5 s, or change `ACTIVE_READING_WINDOW_MS` in `wattwise_platform/lib/device-telemetry.ts` with it.
- **Each accepted insert updates `devices.last_seen_at`.** The `energy_logs_update_device_last_seen` trigger sets `last_seen_at` and `is_online` on the device whose UUID or MAC the row names, if it is paired (`user_id` set). Before platform migration `036` (October 21, 2026) this silently did nothing for anon inserts, so units that last reported earlier show `last_seen_at = null` until their next reading.
- **Inserts are authorized by MAC.** `hardware_anon_insert_energy_logs` accepts an anon insert only when `device_id` matches a registered `devices.mac_address` (case-insensitive). An unregistered or reformatted MAC gets a 401/403.
- **Live updates arrive through Realtime.** The dashboard patches watts, volts, and amps from each `energy_logs` INSERT and no longer reloads the page per reading. It follows relay and budget changes through `devices` UPDATEs. Both tables must stay in the `supabase_realtime` publication.
- **Relay cutoffs are applied by the next poll.** Budget cutoffs, user toggles, and Restore Power all write `devices.relay_state`; the firmware applies the change on its next 5 s relay read. Relay-off in the app is described as a command sent, not a confirmed state.
- **Volume.** At one reading every 5 s a unit adds about 17,000 `energy_logs` rows a day. The platform reads them through bounded, indexed RPCs (`get_latest_device_readings`, `get_usage_kwh_by_device[_day]`, `get_average_watts_by_device`) and an RLS policy evaluated once per query. Do not raise the post rate without checking those.
- **Infrastructure.** Supabase runs in ap-northeast-2 (Seoul), project `akvfnfusmdnqgvurihza`; the web app's Vercel functions run in `icn1` (Seoul). The firmware talks to Supabase directly and never to Vercel.

## Change guidelines

- Preserve the database field names and MAC formatting unless the platform migrations and firmware are changed together.
- Keep Supabase reads narrowly selected and filtered by the device MAC.
- Treat relay behavior as a safety decision. On boot the relay is forced OFF until a successful cloud read; document any failover change.
- Never add a service-role key to firmware. The current anon key is embedded for the prototype, so rely on and verify RLS while this architecture remains.
- Before merging telemetry changes, test a successful POST, a rejected POST, a network outage, a meter reset/decreasing `energy_kwh`, and a relay cutoff response.
- Telemetry aggregation is acknowledged only after an accepted 2xx response; failed batches must remain retryable.

## Related platform files

When changing the payload or access model, inspect the platform migrations in `wattwise_platform/supabase/migrations/`:

- `001_initial_schema.sql`: `devices` and `energy_logs` tables.
- `011_hardware_device_rls.sql` and `022_fix_hardware_energy_log_rls.sql`: the anon device read and `hardware_anon_insert_energy_logs` (`is_registered_device_mac`).
- `014_multi_tenant_manager_tenant.sql`: owner, user, and tenant device access.
- `033_fast_energy_log_rls.sql`: the single `energy_logs` SELECT policy, evaluated once per query.
- `034_bounded_latest_readings_and_report_averages.sql`: index-backed latest readings and report averages.
- `035_capture_update_device_last_seen_trigger.sql` and `036_fix_device_last_seen_for_hardware.sql`: the last-seen trigger and its fix for anon inserts.

The platform's `.github/instructions/supabase-schema-context.instructions.md` documents the current schema, RPCs, and RLS.
