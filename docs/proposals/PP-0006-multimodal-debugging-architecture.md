---
id: PP-0006
title: Multimodal Debugging Architecture for AI Agents
status: draft
author: wezzard
created: 2026-01-06
reviewed: null
decided: null
implemented: null
planned_in: null
---

# Multimodal Debugging Architecture for AI Agents

## Executive Summary

This proposal introduces a multimodal debugging architecture where AI agents consume synchronized trace, video, and audio data through three processing strategies with experiment-driven selection. The architecture enables AI agents to understand not just what the program did (trace), but what the user saw (screen) and intended (voice).

## Motivation

### Current Limitation

ADA currently provides execution traces (ATF format) that tell AI agents *what the program did*. However, for effective debugging, AI agents also need:

- **Visual context**: What the user saw on screen when the bug occurred
- **Intent context**: What the user was trying to accomplish

### Example Scenario

```
User reports: "The app crashes when I click submit"

With trace only:
- AI sees: Function crashed at line 42
- AI doesn't know: Which button? What was on screen? What was user trying to do?

With multimodal context:
- Trace: API call failed with timeout at line 42
- Screen: Shows loading spinner, then error dialog
- Voice: "I'm trying to submit the form after filling in my details"
- AI concludes: "User attempted form submission but API timeout caused crash"
```

## Proposed Architecture

### High-Level Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                MULTIMODAL DEBUGGING SYSTEM                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   CAPTURE LAYER                                                  │
│   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│   │ Trace       │  │ Screen      │  │ Voice       │             │
│   │ (ADA)       │  │ Recording   │  │ Recording   │             │
│   └──────┬──────┘  └──────┬──────┘  └──────┬──────┘             │
│          └────────────────┼────────────────┘                     │
│                           ▼                                      │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │              SYNCHRONIZED CONTEXT BUNDLE                 │   │
│   │  • trace.atf      (execution data)                       │   │
│   │  • screen.mp4     (visual recording)                     │   │
│   │  • voice.m4a      (user narration)                       │   │
│   │  • manifest.json  (timing synchronization)               │   │
│   └─────────────────────────────────────────────────────────┘   │
│                           │                                      │
│                           ▼                                      │
│   PROCESSING LAYER (Three Strategies)                            │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │                 STRATEGY SELECTOR                        │   │
│   │   (Experiment-driven, learns optimal routing)            │   │
│   └─────────────────────────────────────────────────────────┘   │
│          │                │                │                     │
│          ▼                ▼                ▼                     │
│   ┌──────────┐     ┌──────────┐     ┌──────────┐                │
│   │Strategy A│     │Strategy B│     │Strategy C│                │
│   │ Single   │     │ Multi-   │     │ Layered  │                │
│   │ Model    │     │ Model    │     │Processing│                │
│   └──────────┘     └──────────┘     └──────────┘                │
│          │                │                │                     │
│          └────────────────┼────────────────┘                     │
│                           ▼                                      │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │              UNIFIED RESPONSE FORMAT                     │   │
│   └─────────────────────────────────────────────────────────┘   │
│                           │                                      │
│                           ▼                                      │
│   CONSUMER LAYER                                                 │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │                   AI AGENT                               │   │
│   │   Uses multimodal context to debug effectively           │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Three Processing Strategies

All three strategies coexist in the system. The Strategy Selector chooses based on task characteristics and historical experiment results.

#### Strategy A: Single Multimodal Model

```
Question + trace + video + audio
              │
              ▼
     ┌──────────────────┐
     │ Multimodal LLM   │  (GPT-4o, Gemini, Claude)
     │                  │
     │ All modalities   │
     │ processed        │
     │ together         │
     └──────────────────┘
              │
              ▼
         Response
```

**Characteristics:**
- Processes all modalities in a single LLM call
- Maximizes cross-modal reasoning capability
- Highest potential ceiling for complex correlations
- Most expensive, limited by context window

**Best for:** Complex debugging requiring deep correlation across modalities

#### Strategy B: Multi-Model (Specialized)

```
     ┌────────────┐  ┌────────────┐  ┌────────────┐
     │ Trace      │  │ Video      │  │ Audio      │
     │ Processor  │  │ Processor  │  │ Processor  │
     │ (Custom/   │  │ (Vision    │  │ (Whisper/  │
     │  LLM)      │  │  Model)    │  │  ASR)      │
     └─────┬──────┘  └─────┬──────┘  └─────┬──────┘
           │               │               │
           └───────────────┼───────────────┘
                           ▼
              ┌────────────────────┐
              │    Synthesizer     │
              │ (Combines insights)│
              └────────────────────┘
                           │
                           ▼
                      Response
```

**Characteristics:**
- Best-in-class model for each modality
- Cost-efficient (specialized models are cheaper)
- Parallelizable (process modalities concurrently)
- May lose some cross-modal nuance

**Best for:** Tasks where specialized processing matters more than deep correlation

#### Strategy C: Layered Processing

```
┌─────────────────────────────────────────────────────────────┐
│ LAYER 1: EXTRACTION (Cheap, deterministic)                  │
│                                                             │
│ Trace → Structured events, call stacks, timing              │
│ Video → Keyframes, UI state changes, OCR text               │
│ Audio → Transcript with timestamps                          │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ LAYER 2: INDEXING (Structured, queryable)                   │
│                                                             │
│ Unified timeline with synchronized events from all sources  │
│ Searchable by time, content, event type                     │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ LAYER 3: QUERY (AI interprets indexed data)                 │
│                                                             │
│ Natural language → Query plan → Execute on index → Response │
└─────────────────────────────────────────────────────────────┘
```

**Characteristics:**
- Upfront extraction cost, but reusable index
- Efficient for repeated queries on same session
- Structured data enables precise queries
- May lose raw nuance in extraction phase

**Best for:** Multiple queries on same debugging session, simpler questions

### Strategy Selection

The Strategy Selector learns from experiments which strategy works best for each task type:

| Task Type | Typically Best Strategy | Reason |
|-----------|------------------------|--------|
| Crash debugging with user action | Strategy A (Single) | Needs cross-modal correlation |
| Memory leak detection | Strategy B (Multi) | Specialized trace analysis |
| Simple "what happened" | Strategy C (Layered) | Efficient, structured |
| Complex user intent analysis | Strategy A (Single) | Needs full context |

Selection criteria include:
- Task complexity (inferred from question)
- Cost budget
- Latency requirements
- Historical performance on similar tasks

## AI Agent Debugging Journey

The multimodal architecture supports the complete AI agent debugging workflow:

```
Phase 1: TRIGGER
└─► Bug report, CI failure, monitoring alert

Phase 2: REPRODUCE
└─► AI agent launches app with multimodal capture enabled
    $ ada capture start ./app --screen --voice

Phase 3: CAPTURE
└─► Synchronized bundle created (trace + screen + voice)

Phase 4: UNDERSTAND  ◄─── Multimodal processing happens here
└─► AI agent queries: "What happened when the crash occurred?"
    Strategy Selector routes to optimal processing strategy

Phase 5: DIAGNOSE
└─► AI agent forms hypothesis using multimodal insights

Phase 6: FIX
└─► AI agent modifies code, re-runs with capture to verify

Phase 7: VALIDATE
└─► E2E and Field tests confirm fix works
```

## Evaluation Framework

### Experiment-Driven Strategy Comparison

For each task in the evaluation set, run all three strategies and compare:

```
┌─────────────────────────────────────────────────────────────────┐
│   Task: "Find the crash when user clicked submit"               │
│                                                                  │
│   ┌───────────┐  ┌───────────┐  ┌───────────┐                   │
│   │ Strategy A│  │ Strategy B│  │ Strategy C│                   │
│   └─────┬─────┘  └─────┬─────┘  └─────┬─────┘                   │
│         ▼              ▼              ▼                          │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │   Accuracy:   A=95%    B=88%    C=82%                   │   │
│   │   Latency:    A=8s     B=3s     C=1s                    │   │
│   │   Cost:       A=$0.50  B=$0.15  C=$0.05                 │   │
│   │                                                          │   │
│   │   Winner (accuracy): Strategy A                          │   │
│   │   Winner (efficiency): Strategy C                        │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│   Strategy Selector updates routing rules                        │
└─────────────────────────────────────────────────────────────────┘
```

### Evaluation Metrics

| Metric | Description |
|--------|-------------|
| **Bug Identification Accuracy** | Did AI correctly identify the bug? |
| **Root Cause Correctness** | Did AI identify the actual root cause? |
| **Latency** | Time from query to response |
| **Cost** | API/compute cost per query |
| **Improvement over Baseline** | With multimodal vs trace-only |

### Test Categories

| Category | Purpose |
|----------|---------|
| **E2E Tests (Golden)** | Verify processing strategies work correctly on controlled inputs |
| **Field Tests** | Compare strategies on real-world debugging scenarios |
| **Strategy Comparison** | Determine which strategy wins for each task type |
| **Regression Tests** | Ensure changes don't degrade AI agent performance |

## Proposed Implementation

### Directory Structure

```
ada/
├── capture/                     # Capture layer
│   ├── trace/                   # ADA tracer (EXISTS)
│   ├── screen/                  # Screen recording (TO BUILD)
│   │   ├── recorder.py          # AVFoundation/screencapture wrapper
│   │   └── extractor.py         # Keyframe extraction
│   ├── voice/                   # Voice recording (TO BUILD)
│   │   ├── recorder.py          # Audio capture
│   │   └── transcriber.py       # Whisper integration
│   └── bundle.py                # Synchronization and bundling
│
├── processing/                  # Processing layer
│   ├── strategy.py              # Base strategy interface
│   ├── single_model.py          # Strategy A implementation
│   ├── multi_model.py           # Strategy B implementation
│   ├── layered/                 # Strategy C implementation
│   │   ├── extract.py           # Layer 1: Extraction
│   │   ├── index.py             # Layer 2: Indexing
│   │   └── query.py             # Layer 3: Query
│   └── selector.py              # Strategy selection logic
│
├── eval/                        # Evaluation framework
│   ├── experiments/
│   │   ├── runner.py            # Run all strategies on tasks
│   │   ├── comparator.py        # Compare and score results
│   │   └── tasks/               # Evaluation task definitions
│   ├── e2e/                     # E2E tests
│   ├── field/                   # Field tests
│   └── results/                 # Historical results
│
└── config/
    ├── models.yaml              # Model configurations
    └── strategy_rules.yaml      # Learned selection rules
```

### Strategy Interface

```python
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Protocol

@dataclass
class MultimodalContext:
    """Synchronized multimodal debugging context"""
    trace_path: str          # Path to trace.atf
    screen_path: str | None  # Path to screen.mp4 (optional)
    voice_path: str | None   # Path to voice.m4a (optional)
    manifest: dict           # Timing synchronization data

@dataclass
class Response:
    """Unified response format from any strategy"""
    answer: str              # Natural language answer
    evidence: list[dict]     # Supporting evidence with timestamps
    confidence: float        # 0.0 to 1.0
    strategy_used: str       # Which strategy produced this
    latency_ms: float        # Processing time
    cost_usd: float          # Estimated cost

class ProcessingStrategy(Protocol):
    """Interface all strategies must implement"""

    @property
    def name(self) -> str:
        """Strategy identifier (e.g., 'single_model', 'multi_model', 'layered')"""
        ...

    def process(
        self,
        question: str,
        context: MultimodalContext,
    ) -> Response:
        """Process a question against multimodal context"""
        ...

    def estimate_cost(self, context: MultimodalContext) -> float:
        """Estimate cost before processing"""
        ...

    def estimate_latency(self, context: MultimodalContext) -> float:
        """Estimate latency before processing"""
        ...
```

### CLI Extension

```bash
# Start multimodal capture
ada capture start ./app --screen --voice --output ./debug_session/

# Stop capture
ada capture stop

# Query with automatic strategy selection
ada query ./debug_session/ "What caused the crash?"

# Query with specific strategy
ada query ./debug_session/ "What caused the crash?" --strategy single_model

# Run strategy comparison experiment
ada eval compare ./debug_session/ "What caused the crash?"
```

## Implementation Phases

### Phase 1: Capture Infrastructure
- [ ] Screen recording integration (AVFoundation on macOS)
- [ ] Voice recording integration
- [ ] Timestamp synchronization
- [ ] Bundle format specification

### Phase 2: Strategy A (Single Model)
- [ ] Multimodal LLM integration (Claude/GPT-4o/Gemini)
- [ ] Context formatting for each model
- [ ] Response parsing

### Phase 3: Strategy B (Multi-Model)
- [ ] Per-modality processor interfaces
- [ ] Whisper integration for audio
- [ ] Vision model integration for video
- [ ] Synthesizer implementation

### Phase 4: Strategy C (Layered)
- [ ] Extraction layer (OCR, transcription, trace parsing)
- [ ] Indexing layer (unified timeline)
- [ ] Query layer (natural language to structured query)

### Phase 5: Strategy Selector
- [ ] Task classification
- [ ] Historical performance tracking
- [ ] Routing logic

### Phase 6: Evaluation Framework
- [ ] Experiment runner
- [ ] Golden test suite
- [ ] Field test collection
- [ ] Regression tracking

## Dependencies

### New Dependencies Required

| Dependency | Purpose | Notes |
|------------|---------|-------|
| `openai` or `anthropic` | LLM API access | For Strategy A |
| `whisper` | Audio transcription | For Strategy B/C |
| `opencv-python` | Video processing | Keyframe extraction |
| `pytesseract` | OCR | UI text extraction |

### macOS-Specific

- AVFoundation for screen/audio capture
- Alternatively: `screencapture` CLI for screen, `sox` for audio

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| LLM API costs too high | Strategy Selector learns to use cheaper strategies when appropriate |
| Context window limits | Strategy C pre-processes to fit; Strategy B processes in parts |
| Screen recording privacy | Clear user consent; option to disable |
| Voice recording accuracy | Multiple ASR models; fallback to text input |
| Strategy Selector learns wrong patterns | Regular evaluation; manual override option |

## Success Metrics

| Metric | Target |
|--------|--------|
| AI agent debugging accuracy (with multimodal) | >90% on test suite |
| Improvement over trace-only | >25% accuracy improvement |
| Strategy selection accuracy | >80% optimal choice |
| P95 query latency | <10 seconds |
| Cost per debugging session | <$1.00 average |

## Related Documents

- Investigation: `/Users/wezzard/.claude/plans/elegant-fluttering-wozniak.md`
- Query Engine (existing): `query_engine/`
- ADA Tracer: `tracer/`, `tracer_backend/`

## Decisions Made

### 1. Supported Multimodal LLMs

Support multiple providers for Strategy A and model selection experiments:

| Provider | Models | Notes |
|----------|--------|-------|
| **Anthropic** | Claude (Opus, Sonnet) | Primary, native integration |
| **OpenAI** | GPT-4o, GPT-4-turbo | Widely available |
| **Google** | Gemini Pro, Gemini Ultra | Strong multimodal |
| **Minimax** | Minimax models | Alternative provider |
| **ByteDance** | Doubao | Alternative provider |

### 2. Screen Recording

**Decision:** AVFoundation on macOS

- Native framework, best integration
- Supports window-specific capture
- Can capture at variable frame rates
- Handles permissions properly

### 3. Bundle Storage Format

**Decision:** Document Package (directory with extension)

```
debug_session.adabundle/
├── trace.atf           # Execution trace
├── screen.mp4          # Screen recording
├── voice.m4a           # Voice recording
├── manifest.json       # Metadata and timing
└── index/              # Pre-processed index (Strategy C)
    ├── timeline.json
    └── frames/
```

This is the standard macOS document package pattern (like `.app`, `.bundle`, `.playground`).

### 4. Initial Evaluation Task Set

See "Evaluation Task Set Design" section below.

---

## Evaluation Task Set Design

### The Meta Challenge

This product has a unique characteristic:
- **Built FOR AI agents** (consumers are LLMs)
- **Runs ON LLMs** (processing uses LLMs)
- **Evaluated BY LLMs** (AI-as-judge)

This creates potential circularity. Our design must avoid LLM echo chambers.

### Design Principles

1. **Human-verified ground truth** - Every task has a known, human-confirmed bug and root cause
2. **Cross-model evaluation** - Never use the same model for processing AND evaluation
3. **Deterministic baselines** - Include tasks solvable without LLMs (rule-based)
4. **Adversarial cases** - Include scenarios where LLMs typically fail
5. **Multi-model consensus** - Higher confidence when different LLMs agree

### Task Categories

| Category | Description | Complexity Levels |
|----------|-------------|-------------------|
| **Crashes** | Null pointer, assertion, segfault | Simple, Medium, Complex |
| **Memory** | Leaks, use-after-free, corruption | Simple, Medium, Complex |
| **Performance** | Slow functions, blocking calls | Simple, Medium, Complex |
| **Logic** | Wrong output, edge cases | Simple, Medium, Complex |
| **Concurrency** | Race conditions, deadlocks | Medium, Complex |
| **User Intent** | Bug requires understanding user action | Medium, Complex |

### Complexity Definitions

| Level | Trace Only | Multimodal Required | Example |
|-------|------------|---------------------|---------|
| **Simple** | Sufficient | Not needed | Null pointer at obvious location |
| **Medium** | Partially helpful | Helps significantly | Crash after specific UI sequence |
| **Complex** | Insufficient alone | Required | Bug only visible with user intent context |

### Initial Task Set (v1.0)

**Target: 30 tasks minimum**

```
eval/tasks/
├── crashes/
│   ├── crash_001_null_pointer_simple.json
│   ├── crash_002_assertion_medium.json
│   ├── crash_003_stack_overflow_complex.json
│   └── ...
├── memory/
│   ├── memory_001_leak_simple.json
│   ├── memory_002_retain_cycle_medium.json
│   ├── memory_003_use_after_free_complex.json
│   └── ...
├── performance/
│   ├── perf_001_blocking_call_simple.json
│   ├── perf_002_n_squared_medium.json
│   └── ...
├── logic/
│   ├── logic_001_off_by_one_simple.json
│   ├── logic_002_edge_case_medium.json
│   └── ...
├── concurrency/
│   ├── conc_001_race_condition_medium.json
│   ├── conc_002_deadlock_complex.json
│   └── ...
└── user_intent/
    ├── intent_001_wrong_button_medium.json
    ├── intent_002_misunderstood_flow_complex.json
    └── ...
```

### Task Schema

```json
{
  "task_id": "crash_001_null_pointer_simple",
  "category": "crashes",
  "complexity": "simple",
  "multimodal_required": false,

  "inputs": {
    "buggy_code_path": "fixtures/crash_001/app.swift",
    "trace_path": "fixtures/crash_001/trace.atf",
    "screen_path": null,
    "voice_path": null,
    "user_report": "App crashes when I open settings"
  },

  "ground_truth": {
    "bug_type": "null_pointer_dereference",
    "location": "SettingsViewController.swift:42",
    "root_cause": "userPreferences is nil when accessed before initialization",
    "fix_hint": "Add nil check or initialize in viewDidLoad"
  },

  "evaluation": {
    "criteria": [
      {"name": "identified_crash", "weight": 0.3},
      {"name": "correct_location", "weight": 0.3},
      {"name": "root_cause_accuracy", "weight": 0.4}
    ],
    "passing_score": 0.7
  },

  "metadata": {
    "created_by": "human",
    "verified_by": "human",
    "created_date": "2026-01-08",
    "language": "swift",
    "platform": "macos"
  }
}
```

### Evaluation Protocol

```
For each task:
  1. Run all three strategies (A, B, C)
  2. For each strategy output:
     a. Score against ground truth (deterministic)
     b. Run AI-as-judge (different model than processor)
     c. Record latency and cost
  3. Aggregate results by task type and strategy
  4. Update strategy selection rules
```

### Preventing LLM Echo Chambers

| Risk | Mitigation |
|------|------------|
| Same model evaluates its own output | Use different model families for eval |
| LLMs agree on wrong answer | Ground truth is human-verified |
| Evaluation bias toward verbose answers | Structured scoring rubric |
| Models trained on similar data | Include adversarial/novel cases |

### Adversarial Task Examples

1. **Misleading trace**: Bug is in library code, trace shows app code
2. **Red herring**: Multiple issues, only one is the actual bug
3. **Requires domain knowledge**: Bug only visible if you understand iOS lifecycle
4. **Temporal correlation**: Bug happens 5 seconds after user action (not immediately)

---

## Status

**Status: DECIDED**

All key decisions have been made:
- Multi-provider LLM support (5 providers)
- AVFoundation for screen recording
- Document package format (.adabundle)
- Evaluation task set design with 30+ tasks, human-verified ground truth

**Next Step:** Begin implementation Phase 1 (Capture Infrastructure)
