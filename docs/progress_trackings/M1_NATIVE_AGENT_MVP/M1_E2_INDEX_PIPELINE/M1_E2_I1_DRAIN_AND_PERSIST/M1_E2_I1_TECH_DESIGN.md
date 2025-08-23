# Tech Design — M1 E2 I1 Drain and Persist

## Objective
Persist index events from the ring buffer to a simple, durable binary file.

## Design
- File: `<output>/index.idx`
- Header (16 bytes): magic `ADAIDX1\0` (8), record_size (4), reserved (4)
- Record: fixed-size `IndexEvent` as in `tracer_types.h`
- Drain thread: batch read (<=1000), buffered writes; flush on stop

## Out of Scope
- Protobuf encoding; manifest; indexes (beyond header).
