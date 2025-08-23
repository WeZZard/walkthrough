# Backlogs — M1 E3 I1 Backpressure and Metrics

- Detect ring overflow; increment `events_dropped` with basic reason code
- Expose and print metrics: events/sec, drops, drain cycles, bytes written
- Make drain cadence tunable; default 50–100 ms
- Handle `detached` signal: stop drain, finalize file, reset state
