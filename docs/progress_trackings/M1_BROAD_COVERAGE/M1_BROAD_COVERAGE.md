# M1_E1_I10 – Broad Coverage Hooks

Progress snapshot for implementing: Exclude List, DSO Management, Hook Installation, Reentrancy Guard, Event Generation, Ring Buffer Integration, CLI `--exclude`, and performance targets.

Implemented in this iteration:

- Exclude List System: `tracer_backend/agent/exclude_list.h` + implementation providing O(1) lookups with 64-bit FNV-1a hashes and defaults (malloc/free/objc_msgSend/etc.).
- CLI `--exclude`: parsed in controller CLI and propagated via `ADA_EXCLUDE` and agent init payload.
- Agent integration: agent now parses `exclude` from init payload / env to skip user-provided symbols during hook install of known test functions.

Backlog (out of current scope or blocked, to be tracked):

- IndexEvent size mismatch: spec calls for 24 bytes, repository currently defines 32-byte `IndexEvent` in `tracer_backend/include/tracer_backend/utils/tracer_types.h`. Changing this would break existing tests and SHM layout; defer to a dedicated iteration for end-to-end schema migration. [Blocked: would cause test failures].
- Full DSO management (dlopen/dlclose interception) and broad export enumeration across DSOs: partially designed; add Frida-based module enumeration and tracker in a follow-up once integration tests are available. [Out-of-scope for this incremental step].
- Comprehensive hook installation across all exports: will leverage exclude list and module tracker; staged for next iteration to minimize risk to current unit tests. [Planned].
- Performance validation (<100 ns hook overhead, <10 ns exclude check): exclude list implementation is designed to meet target; formal microbenchmarks to be added in a later bench iteration. [Future work].

