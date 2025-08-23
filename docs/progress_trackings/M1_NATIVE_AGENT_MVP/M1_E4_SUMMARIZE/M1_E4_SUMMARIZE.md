# M1 E4: Summarize Command

## Goal
Provide a `tracer summarize <trace>` command to print totals, unique threads, and top function IDs from the persisted index file.

## Deliverables
- Reader for header + fixed-size records
- CLI subcommand `summarize`

## Acceptance
- Summary prints within 1s on sample traces; outputs sane counts

## References
- specs/QUERY_ENGINE_SPEC.md (subset for counts/top-N)
