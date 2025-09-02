# Top-Down Validation Framework for Conflict Resolution

## Overview

When code conflicts arise during development, this framework ensures decisions align with business goals, user stories, and technical specifications - in that priority order.

## The Decision Hierarchy

```
┌─────────────────────────────────────┐  ← HIGHEST PRIORITY
│        BUSINESS GOALS               │
│ "ADA as AI Agent debugging platform"│
└─────────────────────────────────────┘
                  ↓
┌─────────────────────────────────────┐
│          USER STORIES               │
│    "What users need to achieve"     │
└─────────────────────────────────────┘
                  ↓
┌─────────────────────────────────────┐
│        TECHNICAL SPECS              │
│     "How to implement features"     │
└─────────────────────────────────────┘
                  ↓
┌─────────────────────────────────────┐  ← LOWEST PRIORITY
│         IMPLEMENTATION              │
│    "Code, tests, build scripts"     │
└─────────────────────────────────────┘
```

## Validation Process

### Step 1: Business Goal Alignment Check

**Question**: Does this change support ADA's core business goal?

**Core Business Goal** (from `docs/business/BUSINESS_ANALYSIS.md`):
> Build ADA as an intelligent abstraction layer that provides agents with a simple, unified, and cross-platform debugging API to reduce agent development costs and increase reliability.

**Decision Criteria:**

- ✅ **PROCEED**: Change directly enables AI agents to debug programs more effectively
- ✅ **PROCEED**: Change reduces complexity for agent developers  
- ✅ **PROCEED**: Change improves cross-platform compatibility
- ❌ **REJECT**: Change only benefits human debugging workflows
- ❌ **REJECT**: Change increases agent development complexity
- ❌ **REJECT**: Change locks us into single-platform solutions

### Step 2: User Story Validation

**Question**: Does this change fulfill a documented user story?

**Reference**: `docs/user_stories/USER_STORIES.md`

**Primary User Stories by Priority:**

1. **AI Agent wants to programmatically start a program with tracing** (Execution & Tracing #1)
2. **AI Agent wants structured queries against traces** (Interactive Debugging #1)  
3. **AI Agent wants to be notified when trace is complete** (Execution & Tracing #3)
4. **Human Developer wants simple CLI to start tracing** (Execution & Tracing #1)

**Decision Matrix:**

- ✅ **HIGH PRIORITY**: Directly implements a "MUST HAVE" AI Agent story
- ✅ **MEDIUM PRIORITY**: Directly implements a Human Developer story
- ⚠️ **LOW PRIORITY**: Enables a user story but doesn't directly implement it
- ❌ **REJECT**: No clear connection to any documented user story

### Step 3: Technical Specification Compliance

**Question**: Does this change align with technical requirements?

**Reference**: `docs/specs/TRACER_SPEC.md`

**MUST Requirements (FR-xxx):**

- FR-001: Full-coverage function tracing by default
- FR-002: Emit FunctionCall/FunctionReturn events  
- FR-003: Capture ABI-relevant registers
- FR-007: Write durable ATF output
- FR-005: Dynamic module tracking

**Decision Criteria:**

- ✅ **PROCEED**: Directly implements a MUST requirement
- ✅ **PROCEED**: Enables a MUST requirement
- ⚠️ **CONDITIONAL**: Implements SHOULD/MAY requirement (validate against higher levels)
- ❌ **REJECT**: Conflicts with any MUST requirement

### Step 4: Implementation Feasibility

**Question**: Can this be implemented reliably within constraints?

**Constraints:**

- macOS ARM64 (MVP platform)
- Developer Tools permissions required
- Frida-based injection mechanism
- Performance: minimal overhead on traced programs

## Conflict Resolution Examples

### Example 1: "Should we implement interactive breakpoints?"

**Step 1 - Business Goal**: ❌ **REJECT**

- User story "AI Agent wants to set breakpoints" exists
- BUT business goal prioritizes **automated/programmatic** debugging
- Interactive breakpoints serve human workflows, not agent automation
- **Decision**: Don't implement until automated use cases are complete

### Example 2: "Should we return fake PIDs in examples?"

**Step 1 - Business Goal**: ❌ **REJECT**  

- Fake implementations undermine "reliability" business goal
- Creates false confidence in agent developers
- **Decision**: Remove fake examples entirely (already done)

### Example 3: "Should we implement CPU register capture?"

**Step 1 - Business Goal**: ✅ **PROCEED**

- AI agents need register data for debugging analysis
**Step 2 - User Story**: ✅ **HIGH PRIORITY**  
- Maps to AI Agent trace analysis needs
**Step 3 - Tech Spec**: ✅ **PROCEED**
- FR-003: Capture ABI-relevant registers (MUST)
**Decision**: High priority implementation

### Example 4: "Should we support Windows in MVP?"

**Step 1 - Business Goal**: ✅ **PROCEED**

- Cross-platform supports business goal
**Step 2 - User Story**: ⚠️ **IMPLIED**
- No explicit Windows user stories
**Step 3 - Tech Spec**: ❌ **REJECT**
- Tracer spec explicitly scopes to macOS MVP
**Decision**: Defer to post-MVP

## Process Integration

### For Code Reviews

Before approving any substantial change:

1. Reviewer must validate against this framework
2. Changes failing Step 1-2 require product owner approval
3. Changes failing Step 3 require architecture review

### For Feature Planning

Before starting any new feature:

1. Validate it serves the business goal
2. Map it to specific user stories  
3. Check technical specification alignment
4. Estimate implementation effort

### For Bug Fixing

When fixing conflicts between code and specs:

1. **Don't just make it compile** - validate the fix direction
2. If multiple valid approaches exist, choose the one that best serves higher-level goals
3. Update tests and documentation to match the chosen approach

## Escalation Path

When conflicts arise between levels:

1. **Business Goal vs User Story**: Escalate to product owner
2. **User Story vs Tech Spec**: Clarify user story or update spec  
3. **Tech Spec vs Implementation**: Update implementation to match spec
4. **Multiple conflicting specs**: Architectural review required

## Success Metrics

This framework succeeds when:

- ✅ Features directly serve documented user stories
- ✅ Code changes align with business objectives
- ✅ Technical debt is minimized by consistent decision-making
- ✅ Team confidence increases due to clear priorities
- ✅ Agent developers find ADA simple and reliable to use

## Framework Updates

This framework should be updated when:

- Business strategy changes
- New user research emerges  
- Technical constraints change significantly
- User story priorities shift

**Remember**: The goal is not perfect specifications, but **consistent alignment** between business goals, user needs, technical design, and implementation.
