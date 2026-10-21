# WattWise Panel Features and Hardware Hardening

Status (October 22, 2026): both tracks are implemented in code. The platform features below are live; reports were rebuilt on October 21, 2026 to use the same usage RPCs as the dashboard and to price each day at that day's Meralco rate. The hardware items are in `src/main.cpp` on `main`; the acceptance tests listed at the end have not been recorded as run on physical units.

Decisions: MAC pairing, in-app warnings, 80/90/100% thresholds, CSV plus print-friendly PDF, measured-versus-estimated appliance comparison, and relay OFF until a valid cloud command.

Implementation tracks:

- Platform: Meralco component breakdown, deduplicated warning events, bounded daily/weekly/monthly reports, and appliance comparisons.
- Hardware: explicit provisioning diagnostics, BOOT-button reset clarification, JSON relay parsing, URL-safe MAC filters, retry backoff, and 2xx-only telemetry acknowledgement.

Acceptance criteria are recorded in the implementation handoff and must be verified with `npm run lint`, `npm run build`, PlatformIO build, clean provisioning, reset, network outage, relay, meter-reset, report, and budget-warning tests.
