---
id: BH-012
title: Narrative Generation for AI Consumption
status: active
source: docs/specs/QUERY_ENGINE_SPEC.md
---

# Narrative Generation for AI Consumption

## Context

**Given:**
- AI agents need high-level summaries of trace data for debugging
- Raw trace data is too verbose for token-constrained LLMs
- Narratives must highlight anomalies and important patterns
- Results must be actionable (provide evidence references)
- Selective persistence creates mixed data availability (index vs detail)
- Compaction is necessary to fit within token budgets

## Trigger

**When:** An AI agent requests a narrative summary or symptom-driven analysis

## Outcome

**Then:**
- The Query Engine generates compact, bullet-point narratives
- Anomalies are surfaced: crashes, unmatched spans, latency outliers, unusual patterns
- Findings include title, rationale, and evidence references
- Evidence references are stable identifiers (event IDs, span IDs) retrievable later
- Repetitive patterns are folded or summarized
- Token budget is respected via compaction techniques
- Selective persistence windows are annotated (detail vs index-only coverage)
- Results indicate data quality and completeness

## Narrative Structure

### Bullet Points
High-level summary statements:
- "Process crashed at timestamp X in function Y"
- "95th percentile latency spike in function Z (Xms)"
- "Detected N unmatched spans indicating potential cancellations"
- "Thread T blocked for Xms waiting on lock L"

### Findings
Structured anomaly reports:
```json
{
  "title": "Crash in foo::bar",
  "rationale": "SIGSEGV at address 0x0, null pointer dereference",
  "evidenceRefs": ["event:12345", "span:abc"]
}
```

### Evidence References
Stable pointers to trace data:
- `event:12345` → specific event ID
- `span:abc` → specific span ID
- `window:0` → selective persistence window

Retrievable via `events.get`, `spans.list`, etc.

## Compaction Techniques

### Folding Repetitive Patterns
**Given:** Many identical or similar events
**When:** Generating narrative
**Then:**
- Fold into summary: "Function X called 1000 times (avg 5ms, p95 12ms)"
- Use exemplars: Show first, last, and outliers
- Do not list all occurrences

### Exemplar Selection
**Given:** A large set of similar spans
**When:** Compaction is needed
**Then:**
- Select representative examples:
  - Fastest instance
  - Slowest instance
  - Median instance
  - Outliers (p95, p99)
- Include count of folded instances
- Provide evidence refs for exemplars

### Field Elision
**Given:** Token budget is tight
**When:** Compaction is applied
**Then:**
- Use `projection=minimal` by default
- Exclude heavy fields (registers, stack) unless explicitly requested
- Omit redundant fields (e.g., moduleId if module name is present)
- Preserve essential fields for narrative coherence

### Hierarchical Summarization
**Given:** Complex call graphs or long execution flows
**When:** Narrative is constructed
**Then:**
- Summarize at multiple levels:
  - Top-level: "Image processing pipeline executed"
  - Mid-level: "Decode → Transform → Encode"
  - Low-level: Individual function details (on demand)
- Allow drill-down via evidence refs

## Anomaly Detection

### Crashes
**Given:** A SIGSEGV or SIGABRT event is detected
**When:** Narrative is generated
**Then:**
- Highlight crash prominently in bullets
- Include crash location (function, address)
- Provide pre-crash context (N events before)
- Surface any unhandled exceptions or signals

### Latency Outliers
**Given:** Function durations are analyzed
**When:** p95 or p99 exceeds expected thresholds
**Then:**
- Report outlier functions with percentile stats
- Compare to median or average
- Provide example span IDs for investigation

### Unmatched Spans
**Given:** Spans with ENTRY but no RETURN
**When:** Narrative is generated
**Then:**
- Count and report unmatched spans
- Infer reasons (cancellation, crash, timeout) when possible
- Highlight as potential bugs or resource leaks

### Leak-Like Patterns
**Given:** Function pairs like malloc/free or open/close
**When:** Imbalances are detected
**Then:**
- Report: "Function A called X times, function B only Y times"
- Suggest potential resource leak
- Provide evidence refs for unmatched calls

### Hang/Timeout Detection
**Given:** Spans with very long durations or open spans
**When:** Timeout threshold is exceeded
**Then:**
- Report: "Function X blocked for Yms"
- Identify lock contention, I/O waits, or deadlocks if detectable
- Provide span IDs for detailed analysis

## Selective Persistence Integration

### Window Annotation
**Given:** Trace uses selective persistence with detail windows
**When:** Narrative is generated
**Then:**
- Annotate which findings have detail data vs index-only
- Example: "Crash detected (detail available)"
- Example: "Latency spike detected (index-only, no registers)"

### Coverage Ratio
**Given:** Only a subset of events have detail data
**When:** Summary is requested
**Then:**
- Report coverage ratio: "5% of events have detail data"
- Indicate window boundaries in timeline
- State if summary is based primarily on index lane

### Window Triggers
**Given:** Selective persistence windows were triggered by specific events
**When:** Narrative includes those windows
**Then:**
- Report trigger type: "Window triggered by symbol:malloc"
- Indicate trigger timestamp and duration
- Link to trigger event in evidence refs

## Edge Cases

### Empty Trace
**Given:** A trace with zero events
**When:** Narrative is requested
**Then:**
- Return: "No events captured in this trace"
- Do not error; provide informative message

### Extremely Large Trace
**Given:** A trace with billions of events
**When:** Narrative is requested with token budget
**Then:**
- Use aggressive compaction
- Sample if necessary (document sampling in response)
- Focus on top anomalies only
- Set `didTruncate = true`
- Provide pagination for detailed drill-down

### Index-Only Trace (No Detail)
**Given:** No detail lane data is available
**When:** Narrative is generated
**Then:**
- State: "Summary based on index lane only (no detail data)"
- Provide timeline and function call counts
- Cannot provide register/stack analysis
- Focus on high-level patterns

### Mixed Detail Availability
**Given:** Some functions have detail, others don't
**When:** Narrative includes both
**Then:**
- Clearly differentiate: "Function X (detail), Function Y (index-only)"
- Provide richer analysis for functions with detail
- Acknowledge limitations for index-only functions

### Token Budget Too Small
**Given:** Token budget is very small (e.g., 100 tokens)
**When:** Narrative is requested
**Then:**
- Provide minimal summary: "Crash detected in function X"
- Set `didTruncate = true`
- Suggest increasing budget for more detail

## Budget-Aware Tools (Related)

### search.scoped (MUST)
**Purpose:** Neighborhood slice around an anchor span/time

**Input:**
```json
{
  "anchor": { "spanId": "abc123" },
  "lookBackNs": 1000000000,
  "lookAheadNs": 1000000000,
  "filters": { "functionPattern": "process.*" },
  "tokenBudget": 2000
}
```

**Output:**
```json
{
  "spans": [ /* summarized spans */ ],
  "exemplars": [ /* example spans */ ],
  "evidenceRefs": ["span:abc", "event:123"],
  "didTruncate": false
}
```

**Selective Persistence:**
- Prefer `lane=detail` within persisted windows
- Otherwise return index-lane summaries
- Provide pointers to nearest detail window

## Acceptance Criteria (MVP)

### A2: Budget-constrained summaries
**Given:** `narration.summary` and `findings.search` are called with token budgets
**When:** Results are returned
**Then:**
- Results fit within specified tokenBudget (±10%)
- If not, `didTruncate = true` and `nextCursor` provided
- Compaction techniques are applied

### A3: Symptom search efficacy
**Given:** Golden test cases (crash, hang, latency spike)
**When:** `findings.search` is called with symptom text
**Then:**
- Correct area is surfaced in top 3 results
- Rationale explains why the finding is relevant
- Evidence refs point to specific trace data

### A4: Scoped windows
**Given:** `search.scoped` is called with an anchor
**When:** Results are returned
**Then:**
- Compact, correctly ordered neighborhood around anchor
- Exemplars provided for repetitive patterns
- Temporal ordering is maintained

## References

- Original: `docs/specs/QUERY_ENGINE_SPEC.md` (archived source - sections 5, 6, 12)
- Related: `BH-010-query-api` (Query API)
- Related: `BH-011-symbol-resolution` (Symbol Resolution for readable narratives)
- Related: `BH-007-selective-persistence` (Selective Persistence affecting narrative content)
