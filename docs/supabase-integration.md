# Supabase Integration

## Project and endpoints

The firmware targets project `akvfnfusmdnqgvurihza`:

- Relay read: `GET /rest/v1/devices?select=relay_state&mac_address=eq.<MAC>`
- Telemetry write: `POST /rest/v1/energy_logs`

Both requests send the public anon key as `apikey` and as a Bearer token. The key is currently compiled into `src/main.cpp`; this is acceptable only for the controlled prototype and requires strict RLS.

The POST sends no `Prefer` header, so PostgREST uses `return=minimal` and replies `201` with no body. Keep it that way: the anon role cannot read `energy_logs`, so `Prefer: return=representation` would make every insert fail. An anon `GET` on `energy_logs` returns `[]`.

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

What happens after each accepted insert, with the platform migration that defines it:

| Behavior | Migration |
| --- | --- |
| The `hardware_anon_insert_energy_logs` policy accepts an anon insert only when `device_id` matches a registered `devices.mac_address`, ignoring case (`is_registered_device_mac`). | `011`, `022` |
| The smart-budget trigger resolves the device by UUID text or MAC, adds the non-negative cumulative-kWh delta to `device_month_usage`, and may set `devices.relay_state = false` when the budget policy is breached. The next relay poll applies it. | `012`, `025`, `031` |
| `energy_logs_update_device_last_seen` sets `devices.last_seen_at = now()` and `is_online = true` for the paired device. It silently did nothing for anon (hardware) inserts until it became SECURITY DEFINER on October 21, 2026. | `035`, `036` |
| Signed-in users read rows for devices they own, use, or rent through one `energy_logs` SELECT policy evaluated once per query. | `033` |
| The platform reads telemetry through bounded, index-backed RPCs: `get_latest_device_readings`, `get_usage_kwh_by_device[_day]`, `get_average_watts_by_device`. | `005`, `006`, `034` |

`devices.is_online` is never reset to `false`, so the platform decides online status from `last_seen_at` (or the latest reading) being under 20 seconds old. `energy_logs` and `devices` are both in the `supabase_realtime` publication.

## Verified live state

On October 22, 2026, production had three physical units registered:

| Device | MAC | Relay | Budget status | Limit | Last reading |
| --- | --- | --- | --- | --- | --- |
| Room 02 Aircon | `24:0A:C4:B8:11:AA` | true | ok | PHP 500.00 | 2026-05-29 |
| Wattwise Unit Testing | `44:1B:F6:82:2F:50` | true | ok | none | 2026-10-01 |
| Wattwise Unit 2 | `A0:F2:62:E2:0E:BC` | true | ok | PHP 22.75 | 2026-08-12 |

The other ten `devices` rows are admin demo simulations (MACs starting `02:DE`) or seeded entries that have never reported. All three physical units still show `last_seen_at = null` because they last reported before the October 21 trigger fix; it fills in on their next reading.

The same day, the firmware's relay `GET` for `44:1B:F6:82:2F:50` returned `[{"relay_state":true}]` (HTTP 200) with the anon key, and an anon `GET` on `energy_logs` returned `[]`. The anon insert path and the last-seen trigger were verified with a rolled-back insert in a database transaction, not a live POST.

Earlier, on July 27, 2026, a connectivity-verification row was inserted for `44:1B:F6:82:2F:50` with cumulative energy `0.2000`, so the budget delta remained zero. Do not repeat synthetic writes in production data without labeling or cleanup.

