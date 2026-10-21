# WattWise Hardware Agent Guide

## Scope

This repository contains the ESP32-S3 firmware for WattWise sub-meter units. The firmware reads a PZEM-004T v3 meter, controls a relay, serves a local telemetry page, and synchronizes with the WattWise Supabase project. `wattwise_platform` is the consumer of the resulting `energy_logs` rows and the source of the database migrations/RLS policies.

The current source of truth is `main` at commit `f2878c9` (August 12, 2026). Keep `wattwise_cloud` out of scope.

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

## Change guidelines

- Preserve the database field names and MAC formatting unless the platform migrations and firmware are changed together.
- Keep Supabase reads narrowly selected and filtered by the device MAC.
- Treat relay behavior as a safety decision. On boot the relay is forced OFF until a successful cloud read; document any failover change.
- Never add a service-role key to firmware. The current anon key is embedded for the prototype, so rely on and verify RLS while this architecture remains.
- Before merging telemetry changes, test a successful POST, a rejected POST, a network outage, a meter reset/decreasing `energy_kwh`, and a relay cutoff response.
- Telemetry aggregation is acknowledged only after an accepted 2xx response; failed batches must remain retryable.

## Related platform files

When changing the payload or access model, inspect the platform migrations, especially `001_initial_schema.sql`, `011_hardware_device_rls.sql`, and `014_multi_tenant_manager_tenant.sql` in `wattwise_platform/supabase/migrations/`.
