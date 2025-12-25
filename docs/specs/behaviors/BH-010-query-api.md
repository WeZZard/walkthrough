---
id: BH-010
title: Query Engine API and Token Budget Management
status: active
source: docs/specs/QUERY_ENGINE_SPEC.md
---

# Query Engine API and Token Budget Management

## Context

**Given:**
- AI agents consuming trace data have limited token budgets
- Large traces can generate overwhelming amounts of data
- Responses must be compact and actionable for machine consumption
- The Query Engine operates over completed ATF traces
- Multiple query methods are needed for different use cases
- Results must be deterministic and pageable

## Trigger

**When:** An AI agent or user queries the ADA Query Engine for trace analysis

## Outcome

**Then:**
- The Query Engine provides a JSON-RPC 2.0 API over stdio or TCP
- All list/search methods accept optional `tokenBudget`, `projection`, `limit`, and `cursor` parameters
- Responses include `didTruncate` flag and `nextCursor` when data exceeds limits
- Projections control field verbosity: `minimal` (default) excludes heavy fields, `full` includes everything
- Transport uses UTF-8 JSON with optional gzip compression
- Budget-aware compaction reduces output size (fold repeats, use exemplars, elide fields)
- Hard payload cap (e.g., ≤256 KiB per response) enforced server-side
- Deterministic pagination with stable cursors for reproducibility

## API Methods (MVP)

### trace.info (MUST)
**Purpose:** Get high-level trace metadata

**Input:**
```json
{ "tracePath": "/path/to/trace" }
```

**Output:**
```json
{
  "os": "macOS",
  "arch": "arm64",
  "timeStartNs": 1000000000,
  "timeEndNs": 2000000000,
  "eventCount": 1000000,
  "spanCount": 500000,
  "threadCount": 8,
  "taskCount": 42,
  "dropMetrics": {
    "indexLaneDrops": 0,
    "detailLaneDrops": 1500,
    "reasons": { "RING_FULL": 1500 }
  }
}
```

### narration.summary (MUST)
**Purpose:** Generate high-level narrative summary for AI consumption

**Input:**
```json
{
  "tracePath": "/path/to/trace",
  "tokenBudget": 2000,
  "includeCrashes": true,
  "includeHotspots": true,
  "maxFindings": 5
}
```

**Output:**
```json
{
  "bullets": [
    "Process crashed at timestamp 1234567890 in function foo::bar",
    "95th percentile latency spike observed in processImage (200ms)",
    "Detected 3 unmatched spans indicating potential cancellations"
  ],
  "findings": [
    {
      "title": "Crash in foo::bar",
      "rationale": "SIGSEGV at address 0x0, null pointer dereference",
      "evidenceRefs": ["event:12345", "span:abc"]
    }
  ],
  "didTruncate": false
}
```

**Notes:**
- Compress repetitive patterns
- Surface anomalies (crash, unmatched, p95/p99 outliers)
- For selective persistence: include window markers and coverage ratios
- State if summary is based on index-lane only

### findings.search (MUST)
**Purpose:** Symptom-driven retrieval using user/agent text

**Input:**
```json
{
  "text": "memory leak in image processing",
  "hints": {
    "functionPattern": "process.*",
    "modulePattern": "libimage.*",
    "timeRange": { "startNs": 1000000000, "endNs": 2000000000 }
  },
  "tokenBudget": 3000
}
```

**Output:**
```json
{
  "findings": [
    {
      "title": "Potential leak in processImageBuffer",
      "rationale": "Function called 1000 times but matching free only 950 times",
      "evidenceRefs": ["span:xyz"]
    }
  ],
  "didTruncate": false,
  "keySymbolPlan": {
    "moduleContainers": {
      "1": { "type": "bitset", "data": "base64..." }
    }
  }
}
```

**Notes:**
- Use heuristics: crash, hang/timeout, latency spike, leak patterns
- Boost direct matches on names/modules
- Optional output: `keySymbolPlan` for live tracer updates

### spans.list (MUST)
**Purpose:** List spans with filtering

**Input:**
```json
{
  "type": "frame",
  "timeRange": { "startNs": 1000000000, "endNs": 2000000000 },
  "functionPattern": "process.*",
  "modulePattern": "libimage.*",
  "tid": 12345,
  "durationMinNs": 1000000,
  "status": "unmatched",
  "limit": 100,
  "cursor": "abc123",
  "projection": "minimal",
  "tokenBudget": 2000,
  "lane": "auto"
}
```

**Output:**
```json
{
  "items": [
    {
      "spanId": "span123",
      "type": "frame",
      "functionId": 42,
      "name": "processImage",
      "module": "libimage.dylib",
      "tid": 12345,
      "startNs": 1000000000,
      "endNs": 1000005000,
      "durationNs": 5000000,
      "status": "completed"
    }
  ],
  "nextCursor": "def456",
  "didTruncate": false
}
```

**Options:**
- `lane`: "index", "detail", or "auto" (default, prefers detail within windows)

### stats.functionsTopN (MUST)
**Purpose:** Get top N functions by metric

**Input:**
```json
{
  "metric": "p95",
  "topN": 10,
  "timeRange": { "startNs": 1000000000, "endNs": 2000000000 },
  "type": "frame"
}
```

**Output:**
```json
[
  {
    "functionId": 42,
    "name": "processImage",
    "module": "libimage.dylib",
    "count": 1000,
    "totalDurationNs": 5000000000,
    "p50": 4000000,
    "p95": 12000000,
    "p99": 20000000
  }
]
```

### events.get (MUST)
**Purpose:** Retrieve specific events by ID

**Input:**
```json
{
  "eventIds": ["event:12345", "event:12346"],
  "projection": "minimal",
  "lane": "auto"
}
```

**Output:**
```json
{
  "items": [
    {
      "eventId": "event:12345",
      "timestamp": 1000000000,
      "functionId": 42,
      "threadId": 12345,
      "eventKind": "CALL"
    }
  ]
}
```

### plan.keySymbols (MUST)
**Purpose:** Generate key symbol selection plan for live tracer updates

**Input:**
```json
{
  "symptomText": "memory leak in image processing",
  "hints": {
    "files": ["image_processor.c"],
    "modules": ["libimage.dylib"],
    "functions": ["processImage"]
  },
  "topK": 500
}
```

**Output:**
```json
{
  "moduleContainers": {
    "1": {
      "type": "bitset",
      "data": "base64encodeddata",
      "version": "v1.2.3",
      "hash": "abc123"
    }
  },
  "rationale": [
    {
      "functionId": 42,
      "score": 0.95,
      "reasons": ["direct_match", "type_match", "async_adjacent"]
    }
  ]
}
```

**Behavior:**
- Build candidate symbols via DWARF/name/type/param matching
- Expand async-adjacent neighbors
- Rank by relevance score
- Emit per-module containers chosen by density (bitset/roaring/hash)

## Edge Cases

### Token Budget Exceeded
**Given:** Query results exceed the specified token budget
**When:** The response is being prepared
**Then:**
- Apply compaction (fold repeats, use exemplars, elide fields)
- If still too large after compaction, set `didTruncate = true`
- Return `nextCursor` for pagination
- Ensure response fits within token budget (±10% tolerance)

### Payload Cap Enforcement
**Given:** Compacted response still exceeds hard payload cap (256 KiB)
**When:** The response is being serialized
**Then:**
- Truncate to fit within cap
- Set `didTruncate = true`
- Return `nextCursor` for next page
- Log warning about excessive result size

### Pagination Determinism
**Given:** Multiple requests with the same cursor
**When:** Pagination is requested
**Then:**
- Return identical results for the same cursor
- Maintain stable ordering (e.g., by timestamp, then event ID)
- Cursor encodes position and query parameters
- Results are reproducible across queries

### Missing Detail Data
**Given:** Query requests detail lane but no detail file exists
**When:** Processing the query
**Then:**
- Fall back to index lane automatically
- Document in response that detail was unavailable
- Do not error; provide best available data

### Selective Persistence Windows
**Given:** Query spans multiple selective persistence windows
**When:** Constructing results
**Then:**
- Prefer detail lane within persisted windows
- Use index lane outside windows
- Annotate which spans have detail vs index-only
- Include window boundaries in response metadata

### Cross-Thread Merge
**Given:** Query requires cross-thread analysis
**When:** Processing spans.list or narrative
**Then:**
- Load all relevant thread files
- Merge-sort by timestamp
- Maintain per-thread context
- Results show cross-thread causality

## Performance Targets

- **P-001 Token budgets:** All methods attempt to fit within tokenBudget using compaction
- **P-002 Latency targets:** p50 ≤ 100 ms, p95 ≤ 500 ms on medium traces (≤1e6 events) on dev hardware
- **P-003 Payload caps:** Server-side hard cap (≤256 KiB per response) in addition to tokenBudget
- **P-004 Deterministic paging:** Stable ordering and cursors for reproducibility

## References

- Original: `docs/specs/QUERY_ENGINE_SPEC.md` (archived source - sections 1-6, 9, 12)
- Related: `BH-011-symbol-resolution` (Symbol Resolution)
- Related: `BH-012-narrative-generation` (Narrative Generation)
