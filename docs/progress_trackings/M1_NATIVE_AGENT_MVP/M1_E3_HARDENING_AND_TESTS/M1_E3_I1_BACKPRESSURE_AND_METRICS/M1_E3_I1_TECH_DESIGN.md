# Tech Design — M1 E3 I1 Backpressure and Metrics

## Objective
Add drop accounting, drain cadence tuning, and metrics exposure.

## Design
- Ring buffer: track read/write positions; increment drops on overflow
- Controller stats: events/sec, drops, drain cycles, bytes written
- Drain cadence: 50–100 ms sleep; wake on activity (later)

## Out of Scope
- Sharded per-thread rings; priority lanes.
