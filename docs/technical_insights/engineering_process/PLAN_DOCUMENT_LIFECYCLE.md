# Plan Document Lifecycle

This document defines the status header convention and append-only principle for iteration plan documents in the ADA project.

## Status Header Convention

All iteration plan documents (TECH_DESIGN.md, TEST_PLAN.md, BACKLOGS.md) should include a YAML frontmatter status header at the top of the file:

```yaml
---
status: active | completed | superseded | abandoned
superseded_by: M1_E5_I1_ATF_V2_WRITER  # if superseded
date: 2025-01-XX
reason: "Brief explanation"  # if abandoned or superseded
---
```

## Status Definitions

| Status | Description |
|--------|-------------|
| **active** | Currently being worked on |
| **completed** | Finished and accepted into the codebase |
| **superseded** | Replaced by a newer iteration (link provided) |
| **abandoned** | Not completed, with documented reason |

## Append-Only Principle

Plan documents are **historical records**. They capture the thinking and decisions made at a specific point in time. Never delete or modify completed plans.

### Why Append-Only?

1. **Preserves context** - Future readers can understand why decisions were made
2. **Enables learning** - Teams can review what was planned vs what was built
3. **Supports auditing** - Clear trail of design evolution
4. **Prevents confusion** - Old references remain valid

### How to Handle Supersession

When a plan needs to be replaced:

1. **Mark the old plan as superseded** - Add status header with `superseded_by` link
2. **Create a new iteration** - Use fresh documents in a new folder
3. **Preserve the body** - Do not modify the original plan content
4. **Document the reason** - Explain why the change was necessary

### Example: Superseding an Iteration

**Old iteration** (`M1_E2_I3_TECH_DESIGN.md`):
```yaml
---
status: superseded
superseded_by: M1_E5_I1_ATF_V2_WRITER
date_superseded: 2025-01-15
reason: "Protobuf encoding overhead incompatible with streaming throughput requirements. Replaced with raw binary format."
---

# Original Tech Design Content
(unchanged)
```

**New iteration** (`M1_E5_I1_TECH_DESIGN.md`):
```yaml
---
status: active
date: 2025-01-15
supersedes: M1_E2_I3_ATF_V4_WRITER
---

# New Tech Design Content
...
```

## Epic-Level Handling

When creating a new epic that supersedes iterations from completed epics:

1. **Do not extend completed epics** - Create a new epic instead
2. **Document cross-cutting concerns** - Explain why the work spans multiple original epics
3. **Link supersession clearly** - Each new iteration should reference what it supersedes

### Example: Cross-Cutting Epic

```markdown
# M1_E5: ATF V2 - Raw Binary Trace Format

## Why a New Epic?
- Completed epics (E2, E4) should not be extended
- ATF format is cross-cutting (touches both writer and reader)
- Clean separation of concerns

## Supersession Map
| New Iteration | Supersedes |
|---------------|------------|
| M1_E5_I1 | M1_E2_I3 (ATF V4 Writer) |
| M1_E5_I2 | M1_E4_I1 (ATF Reader) |
```

## Folder Structure

```
docs/progress_trackings/M1_NATIVE_AGENT_MVP/
├── M1_E2_INDEX_PIPELINE/
│   └── M1_E2_I3_ATF_V4_WRITER/          <- superseded (header added)
│       ├── M1_E2_I3_TECH_DESIGN.md
│       ├── M1_E2_I3_TEST_PLAN.md
│       └── M1_E2_I3_BACKLOGS.md
├── M1_E4_QUERY_ENGINE/
│   └── M1_E4_I1_ATF_READER/             <- superseded (header added)
│       ├── M1_E4_I1_TECH_DESIGN.md
│       ├── M1_E4_I1_TEST_PLAN.md
│       └── M1_E4_I1_BACKLOGS.md
└── M1_E5_ATF_V2/                         <- new epic
    ├── M1_E5_ATF_V2.md                   <- epic target doc
    ├── M1_E5_I1_ATF_V2_WRITER/           <- new iteration
    │   ├── M1_E5_I1_TECH_DESIGN.md
    │   ├── M1_E5_I1_TEST_PLAN.md
    │   └── M1_E5_I1_BACKLOGS.md
    └── M1_E5_I2_ATF_V2_READER/           <- new iteration
        ├── M1_E5_I2_TECH_DESIGN.md
        ├── M1_E5_I2_TEST_PLAN.md
        └── M1_E5_I2_BACKLOGS.md
```

## Benefits

1. **Traceability** - Every plan change is documented and linked
2. **Clarity** - Current status is immediately visible in headers
3. **Flexibility** - Can supersede across epic boundaries when needed
4. **History** - Full record of design evolution preserved
