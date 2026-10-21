# WattWise Hardware Documentation

This folder documents the firmware as implemented in `src/main.cpp` and its integration with `wattwise_platform`.

- [Architecture](architecture.md): board, sensor, relay, timing, and local web behavior.
- [Supabase integration](supabase-integration.md): REST endpoints, payloads, RLS, and platform trigger effects.
- [Operations](operations.md): provisioning, verification, demo safety, and known limitations.

These notes describe the current prototype. Last checked against the platform and production database on October 22, 2026. The platform behaviors the firmware depends on are summarized under "Platform contract" in [`AGENTS.md`](../AGENTS.md); confirm the platform migrations before changing the live schema contract.

