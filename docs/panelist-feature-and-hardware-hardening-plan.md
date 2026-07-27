# WattWise Panel Features and Hardware Hardening

This plan is now being implemented across `wattwise_platform` and `wattwise_hardware`.

Decisions: MAC pairing, in-app warnings, 80/90/100% thresholds, CSV plus print-friendly PDF, measured-versus-estimated appliance comparison, and relay OFF until a valid cloud command.

Implementation tracks:

- Platform: Meralco component breakdown, deduplicated warning events, bounded daily/weekly/monthly reports, and appliance comparisons.
- Hardware: explicit provisioning diagnostics, BOOT-button reset clarification, JSON relay parsing, URL-safe MAC filters, retry backoff, and 2xx-only telemetry acknowledgement.

Acceptance criteria are recorded in the implementation handoff and must be verified with `npm run lint`, `npm run build`, PlatformIO build, clean provisioning, reset, network outage, relay, meter-reset, report, and budget-warning tests.
