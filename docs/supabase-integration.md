# Supabase Integration

## Project and endpoints

The firmware targets project `akvfnfusmdnqgvurihza`:

- Relay read: `GET /rest/v1/devices?select=relay_state&mac_address=eq.<MAC>`
- Telemetry write: `POST /rest/v1/energy_logs`

Both requests send the public anon key as `apikey` and as a Bearer token. The key is currently compiled into `src/main.cpp`; this is acceptable only for the controlled prototype and requires strict RLS.

## Telemetry payload

```json
{
  "device_id": "44:1B:F6:82:2F:50",
  "average_watts": 0.0,
  "energy_kwh": 0.2,
  "voltage_v": 237.8,
  "current_a": 0.0
}
```

`recorded_at` is omitted by the firmware and is assigned by the database default. `energy_kwh` is a cumulative meter reading, not the interval delta.

## Platform-side behavior

`wattwise_platform` permits anon inserts only when `energy_logs.device_id` matches a registered `devices.mac_address` (`011_hardware_device_rls.sql`). Its smart-budget trigger resolves the device by UUID text or MAC, computes a non-negative cumulative-kWh delta, updates `device_month_usage`, and may set `devices.relay_state = false` when the budget policy is breached. The next hardware poll then applies that relay state physically.

## Verified live state

On July 27, 2026, the configured endpoint returned these registered devices:

| MAC | Relay | Budget status | Limit |
| --- | --- | --- | --- |
| `24:0A:C4:B8:11:AA` | true | ok | PHP 500.00 |
| `44:1B:F6:82:2F:50` | true | ok | none |

A connectivity-verification row was successfully inserted for `44:1B:F6:82:2F:50` with cumulative energy `0.2000`, so the budget delta remained zero. Do not repeat synthetic writes in production data without labeling or cleanup.

