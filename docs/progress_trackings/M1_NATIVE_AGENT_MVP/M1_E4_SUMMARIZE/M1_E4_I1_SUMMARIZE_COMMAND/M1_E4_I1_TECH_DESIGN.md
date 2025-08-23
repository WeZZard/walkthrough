# Tech Design — M1 E4 I1 Summarize Command

## Objective
Add `tracer summarize <trace.idx>` to read header + records and print a brief summary.

## Design
- Parse 16B header; verify magic; read fixed-size records
- Compute totals, unique threads, top function IDs
- Print simple tabular output

## Out of Scope
- Symbol resolution; URL/JSON output; Protobuf.
