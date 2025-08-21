# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## MANDATORY: Project Structure

- This is a mixed language repository containing Rust, C/C++, and Python code.
- You **MUST** put files by stricly following the project structure definition mentioned below:

```plaintext
project-root/
├── Cargo.toml                      # Project-level Rust Cargo crate manifest file
├── docs/                           # Project-level documentation
│   ├── business/                   # Business analysis
│   │    └── BUSINESS_ANALYSIS_DOC_TITLE.md
│   ├── user_stories/               # User stories
│   │    └── USER_STORY_DOC_TITLE.md
│   ├── specs/                      # Tech specifications
│   │    └── SPEC_DOC_TITLE.md
│   ├── technical_insights/         # Technical insights
│   │    └── TECHNICAL_INSIGHT_DOC_TITLE.md
│   ├── engineering_efficiency/     # Best practices and tooling guides
│   │    └── ENGINEERING_EFFICIENCY_DOC_TITLE.md
│   └── progress_trackings/         # Project progress trackings
│        ├─── M1_MILESTONE_NAME_1/                       # Milestone 1 folder
│        │     ├─── M1_MILESTONE_NAME_1.md               # Milestone 1 description
│        │     ├─── M1_E1_EPIC_NAME_1/                   # Milestone 1 epic 1 folder
│        │     │     ├─── M1_E1_EPIC_NAME_1.md           # Milestone 1 epic 1 description
│        │     │     ├─── M1_E1_I1_ITERATION_NAME_1/     # Milestone 1 epic 1 iteration 1 folder
│        │     │     │    ├─── M1_E1_I1_TECH_DESIGN.md   # Milestone 1 epic 1 iteration 1 tech design doc
│        │     │     │    ├─── M1_E1_I1_TEST_PLAN.md     # Milestone 1 epic 1 iteration 1 test plan doc
│        │     │     │    └─── M1_E1_I1_BACKLOGS.md      # Milestone 1 epic 1 iteration 1 backlogs doc
│        │     │     └─── M1_E1_I2_ITERATION_NAME_2/     # Milestone 1 epic 1 iteration 2 folder
│        │     │          ├─── M1_E1_I2_TECH_DESIGN.md   # Milestone 1 epic 1 iteration 2 tech design doc
│        │     │          ├─── M1_E1_I2_TEST_PLAN.md     # Milestone 1 epic 1 iteration 2 test plan doc
│        │     │          └─── M1_E1_I2_BACKLOGS.md      # Milestone 1 epic 1 iteration 2 backlogs doc
│        │     └─── M1_E2_EPIC_NAME_2/                   # Milestone 1 epic 2 folder
│        │           ├─── M1_E2_EPIC_NAME_2.md           # Milestone 1 epic 2 description
│        │           ├─── M1_E2_I1_ITERATION_NAME_1/     # Milestone 1 epic 2 iteration 1 folder
│        │           │    ├─── M1_E2_I1_TECH_DESIGN.md   # Milestone 1 epic 2 iteration 1 tech design doc
│        │           │    ├─── M1_E2_I1_TEST_PLAN.md     # Milestone 1 epic 2 iteration 1 test plan doc
│        │           │    └─── M1_E2_I1_BACKLOGS.md      # Milestone 1 epic 2 iteration 1 backlogs doc
│        │           └─── M1_E2_I2_ITERATION_NAME_2/     # Milestone 1 epic 2 iteration 2 folder
│        │                ├─── M1_E2_I2_TECH_DESIGN.md   # Milestone 1 epic 2 iteration 2 tech design doc
│        │                ├─── M1_E2_I2_TEST_PLAN.md     # Milestone 1 epic 2 iteration 2 test plan doc
│        │                └─── M1_E2_I2_BACKLOGS.md      # Milestone 1 epic 2 iteration 2 backlogs doc
│        └─── M2_MILESTONE_NAME_2/                       # Milestone 2 folder
│              ├─── M2_MILESTONE_NAME_2.md               # Milestone 2 description
│              ├─── M2_E1_EPIC_NAME_1/                   # Milestone 2 epic 1 folder
│              │     ├─── M2_E1_EPIC_NAME_1.md           # Milestone 2 epic 1 description
│              │     ├─── M2_E1_I1_ITERATION_NAME_1/     # Milestone 2 epic 1 iteration 1 folder
│              │     │    ├─── M2_E1_I1_TECH_DESIGN.md   # Milestone 2 epic 1 iteration 1 tech design doc
│              │     │    ├─── M2_E1_I1_TEST_PLAN.md     # Milestone 2 epic 1 iteration 1 test plan doc
│              │     │    └─── M2_E1_I1_BACKLOGS.md      # Milestone 2 epic 1 iteration 1 backlogs doc
│              │     └─── M2_E1_I2_ITERATION_NAME_2/     # Milestone 2 epic 1 iteration 2 folder
│              │          ├─── M2_E1_I2_TECH_DESIGN.md   # Milestone 2 epic 1 iteration 2 tech design doc
│              │          ├─── M2_E1_I2_TEST_PLAN.md     # Milestone 2 epic 1 iteration 2 test plan doc
│              │          └─── M2_E1_I2_BACKLOGS.md      # Milestone 2 epic 1 iteration 2 backlogs doc
│              └─── M2_E2_EPIC_NAME_2/                   # Milestone 2 epic 2 folder
│                    ├─── M2_E2_EPIC_NAME_2.md           # Milestone 2 epic 2 description
│                    ├─── M2_E2_I1_ITERATION_NAME_1/     # Milestone 2 epic 2 iteration 1 folder
│                    │    ├─── M2_E2_I1_TECH_DESIGN.md   # Milestone 2 epic 2 iteration 1 tech design doc
│                    │    ├─── M2_E2_I1_TEST_PLAN.md     # Milestone 2 epic 2 iteration 1 test plan doc
│                    │    └─── M2_E2_I1_BACKLOGS.md      # Milestone 2 epic 2 iteration 1 backlogs doc
│                    └─── M2_E2_I2_ITERATION_NAME_2/     # Milestone 2 epic 2 iteration 2 folder
│                         ├─── M2_E2_I2_TECH_DESIGN.md   # Milestone 2 epic 2 iteration 2 tech design doc
│                         ├─── M2_E2_I2_TEST_PLAN.md     # Milestone 2 epic 2 iteration 2 test plan doc
│                         └─── M2_E2_I2_BACKLOGS.md      # Milestone 2 epic 2 iteration 2 backlogs doc
│
├── [python_component_name]/        # Python component
│   ├── Cargo.toml                  # Python component Rust Cargo crate manifest file
│   ├── pyproject.toml
│   ├── src/                        # Python component Rust Cargo crate dir
│   │   └── lib.rs                  # Python component Rust Cargo crate definition
│   ├── docs/                       # Python component documentations
│   │   └── design/                 # Python component tech designs
│   ├── [python_component_name]/    # Python component sources
│   │   └── [modules]/              # Python component module soruces
│   └── tests/                      # Python component tests
│       ├── bench/                  # Python component benchmarks
│       ├── integration/            # Python component integration tests
│       ├── unit/                   # Python component unit tests  
│       └── fixtures/               # Python component test data/programs
│
├── [c_cpp_component_name]/         # C/C++ component
│   ├── Cargo.toml                  # C/C++ component Rust Cargo crate manifest file
│   ├── CMakeLists.txt              # C/C++ component CMakeLists
│   ├── docs/                       # C/C++ component documentations
│   │   └── design/                 # C/C++ component tech designs
│   ├── include/                    # C/C++ component headers
│   │   └── [modules]/              # C/C++ component module public headers
│   ├── src/                        # C/C++ component sources
│   │   └── [modules]/              # C/C++ component module sources
│   └── tests/                      # C/C++ component tests
│       ├── bench/                  # C/C++ component benchmarks
│       ├── integration/            # C/C++ component integration tests
│       ├── unit/                   # C/C++ component unit tests
│       └── fixtures/               # C/C++ component test data/programs
│
├── [rust_component_name]/          # Rust component
│   ├── Cargo.toml                  # Rust component Cargo crate manifest file
│   ├── docs/                       # Rust component documentation
│   │   └── design/                 # Rust component technical designs
│   ├── src/                        # Rust component sources
│   │   └── [modules]/              # Rust component source modules
│   └── tests/                      # Rust component tests
│       ├── bench/                  # Rust component benchmarks
│       ├── integration/            # Rust component integration tests
│       ├── unit/                   # Rust component unit tests  
│       └── fixtures/               # Rust component test data/programs
│
├── utils/                          # Scripts for engineering efficiency
│
├── tools/                          # Tools to complete or enhance the product workflow.
│
├── third_parties/                  # Third parties dependencies like Frida SDK.
│   ├── frida-core/                 # Frida core SDK
│   ├── frida-gum/                  # Frida gum SDK
│   ├── frida-core-devkit-17.2.16-macos-arm64.tar.xz   # Downloaded Frida core SDK archive (git-ignored)
│   └── frida-gum-devkit-17.2.16-macos-arm64.tar.xz    # Downloaded Frida gum SDK archive (git-ignored)
│
└── target/                         # Built products of the project (ignored in git)
```

**Document deprecations**:

Deprecated documents shall be renamed with a `[DEPRECATED]` prefix.

**Rules for maintaining the project structure:**

1. **Directories for all the components MUST named with snake_style convention.**
1. **MUST place project-level docs in the /docs/ directory and component-level docs in the {component}/docs directory** - Use appropriate subdirectories
2. **MUST place all the progress tracking documents in the /docs/progress_trackings directory** - Use appropriate subdirectories
3. **NEVER commit compiled binaries** - Add to .gitignore  

## MANDATORY: Components

- tracer: The tracer, written in Rust.
- tracer-backend(dir name: `tracer_backend`): The backend of the traer, written in C/C++.
- query-engine(dir name: `query_engine`): The query engine, written in Python.
- mcp-server(dir name: `mcp_server`): The MCP server of the entire system, written in Python.

## MANDATORY: Development Model

The development of this repository is driven by user story map, user stories and specifications.

### Workflow

This workflow organizes work into three levels:

- Milestones – High-level objectives with a target document describing overall goals.
- Epics – Thematic bodies of work aligned with a milestone, each with an epic target document.
- Iterations – Execution units within an epic, each containing:
  1. Technical design (how the solution is structured)
  2. Test plan (how the solution is validated)
  3. Backlogs (the tasks and stories to deliver)

### Action Order in Iterations

Execute each iteration with the following action order, aligned with its three required artifacts (Technical Design, Test Plan, Backlogs):  

1. **Iteration setup (Plan)** – Finalize Technical Design (structure/approach), Test Plan (test matrix, coverage goals), and Backlogs (granular tasks with acceptance criteria).  
   - Entry criteria: artifacts approved and linked to stories/epics.  
2. **Skeletons (Prepare)** – Create minimal, compilable skeletons per design: workspace wiring, modules, and public interfaces.  
   - Gate: project compiles successfully.  
3. **Unit tests first (Specify)** – From the Test Plan, write failing unit tests for each backlog item and public interface.  
   - Gate: tests fail for the right reasons.  
4. **Implement with TDD (Build)** – Implement functions incrementally until all unit tests pass; refactor safely as needed.  
   - Gate: all unit tests pass; no regressions.  
5. **Module integration (Verify)** – Add and run module-level integration tests to validate collaboration within modules.  
   - Gate: all module-level tests pass before proceeding.  
6. **System integration (Validate)** – Add and run system-level integration tests across modules in scope of the iteration/epic.  
   - Gate: all system-level tests pass before proceeding.  
7. **User story validation (Accept)** – Execute story-level acceptance tests derived from user stories to verify functional outcomes.  
   - Gate: all acceptance tests pass before proceeding.  
8. **Performance and NFRs (Prove)** – Run benchmarks and non-functional checks (performance, reliability, security) specified in the Test Plan.  
   - Gate: meets specified thresholds; all tests pass before finalizing.  
9. **Definition of Done (Close)** – Ensure coverage goals, documentation updates, and traceability are complete. Update artifacts to reflect reality.  
   - Gate: 100% builds/tests pass; coverage meets quality gates; backlog items closed with evidence.  

See also:

- [Quality Gate Enforcement and Integration Metrics](docs/engineering_efficiency/QUALITY_GATE_ENFORCEMENT_AND_INTEGRATION_METRICS.md)
- [Top Down Validation Framework](docs/engineering_efficiency/TOP_DOWN_VALIDATION_FRAMEWORK.md)
- [Integration Process Specification](docs/engineering_efficiency/INTEGRATION_PROCESS_SPECIFICATION.md)

## MANDATORY: Engineering Architecture

### MANDATORY: Build System Orchestration

Required: **CARGO IS THE SINGLE DRIVER FOR ALL BUILDS - NO EXCEPTIONS**

The entire project build system MUST be orchestrated through Cargo:

1. **ALL components** (Rust, C/C++, Python) are built via `cargo build`
2. **C/C++ components** are built as leaves via build.rs using the cmake crate
3. **Python components** are built via maturin invoked from Cargo
4. **NEVER** run CMake directly - always use `cargo build`
5. **NEVER** create standalone build scripts that bypass Cargo

### Build System Overview

The project uses a polyglot build system with Cargo as the mandatory orchestrator:

- **Rust Components**: Built directly with Cargo
- **C/C++ Components**: Built via build.rs using cmake crate - NEVER invoke CMake directly
- **Python Components**: Built via Cargo-driven maturin — NEVER invoke maturin/setup.py/pip build directly
- **C/C++ Compilation Database**: compile_commands.json must be generated by CMake (via build.rs/cmake crate) and emitted under target/ or the configured build dir; IDEs should be pointed to that path (do not write to repo root).

### Mission & Operating Rules

- **Single driver**: Cargo orchestrates EVERYTHING. C/C++ is built as a leaf via build.rs (cmake crate). Python wheels are produced with maturin.
- **Idempotent & reproducible**: Never write outside target/ or the chosen build dir. Prefer pinned tool versions where possible.
- **Fail fast with context**: If a step fails, surface the exact command, tool versions, env vars, and last 50 lines of logs.
- **Editor integration (C/C++)**: Always enable CMake's compile_commands.json via build.rs/cmake crate, emit it under target/ or the configured build dir, and surface its absolute path in build output for IDE consumption; never write it to the repo root.

### Common Build Commands

```bash
# Initialize third-party dependencies (MUST run first)
./utils/init_third_parties.sh

# Build all components (when project structure is populated)
cargo build --release

# Build specific component
cd [component_name] && cargo build --release

# Build Python wheels with maturin (when Python components exist)
maturin build --release

# Run tests for all components
cargo test --all

# Run tests with coverage (requires cargo-llvm-cov)
cargo llvm-cov --all-features

# Build C/C++ components separately (when CMakeLists.txt exists)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make -j$(nproc)
# compile_commands.json will be generated in this build directory
```

### IDE/Editor Integration

#### VSCode-based IDEs (Cursor, VSCode, etc.)

**Preferred Approach**: Use VSCode's built-in debugging capabilities with `launch.json` configurations:

- **Debugging**: Configure `launch.json` for integrated debugging experience
- **Running**: Use Run/Debug configurations in the IDE
- **IntelliSense**: Point C/C++ extension to `/target/compile_commands.json`

Example `launch.json` configurations should be provided in `.vscode/launch.json` for:
- Rust tests and binaries (using CodeLLDB or rust-analyzer)
- C/C++ tests and examples (using CodeLLDB or gdb/lldb)
- Python components (using Python debugger)

**Key Points**:
- compile_commands.json is generated automatically by the CMake build invoked from build.rs and is placed under target/ (or the configured build directory)
- Multi-component workspaces can produce multiple compile_commands.json files; point your IDE to the one corresponding to the component you are editing
- Do not copy or symlink compile_commands.json to the repo root

#### CLI Integration

**Preferred Approach**: Use Cargo commands directly:

```bash
# Build everything
cargo build --release

# Run tests
cargo test

# Run specific test
cargo test test_name

# Run with verbose output
cargo test -- --nocapture
```

For non-Rust binaries (C/C++ examples, tests), access them via predictable paths:
```bash
# Run Frida examples
./target/release/frida_examples/frida_gum_example
./target/release/frida_examples/frida_core_example <pid>

# Run C/C++ tests
./target/release/tracer_backend/test/test_ring_buffer
```

**Important**: Do NOT create wrapper shell scripts - use Cargo commands or direct binary execution

All code in this repository must be covered by automated tests and measured with code coverage tools:

- Rust – Use cargo built-in support (e.g., cargo tarpaulin or cargo llvm-cov).
- C / C++ – Organize with CMake and use LLVM coverage tools (e.g., llvm-profdata, llvm-cov) to calculate line coverage.
- Python – Use community standard tools (e.g., pytest, coverage.py) for reporting.

### Third-Party Dependencies

The project uses Frida SDK for dynamic instrumentation. Dependencies are managed in `/third_parties/`:

```bash
# Initialize Frida SDKs (required before first build)
./utils/init_third_parties.sh

# Directory structure after initialization:
third_parties/
├── frida-core/          # Frida Core SDK (controller-side)
├── frida-gum/           # Frida Gum SDK (agent-side)
└── *.tar.xz            # Downloaded archives (git-ignored)
```

Key points:

- Archives are git-ignored to keep repository size manageable
- Platform-specific SDKs are downloaded automatically (macos-arm64, linux-x86_64, etc.)
- CMake integration via `utils/cmake/FindFrida.cmake`
- Rust integration via build.rs using cmake crate

### Toolchain & Package Management Systems

- Rust: stable
- C/C++: CMake and clang
- Python: maturin

### Cross-Platform Notes

- macOS universal2: maturin build --release --target universal2-apple-darwin  
- Linux wheels: maturin build --release --manylinux 2014  
- Windows: ensure MSVC matches Python ABI.

Use .cargo/config.toml for non-default linkers/targets.  

### Versioning & Releases

- Rust crates: bump Cargo.toml, tag vX.Y.Z.  
- Python: bump pyproject.toml.  
- Keep CHANGELOG.md at root.

### Troubleshooting

- Old/missing CMake: cmake --version.  
- Link errors: check println!(cargo:rustc-link-lib=...).  
- Python import fails: ensure maturin develop ran in correct venv.  
- ABI mismatch: sync C headers with Rust extern signatures.  
- Crash and unexpected behaviors: use LLDB or pdb to conduct an interactive debug.

## Business Architecture

The ADA system is a comprehensive tracing and analysis platform with the following core architecture:

### Two-Lane Flight Recorder Architecture

The tracer uses a dual-lane recording system inspired by aircraft black boxes:

1. **Index Lane** (Always-On): Lightweight 24-byte events capturing function flow
   - Timestamp, function ID, thread ID, event type
   - Continuous recording with minimal overhead
   - Provides complete execution timeline

2. **Detail Lane** (Triggered): Heavy 512-byte events with full context
   - Includes register values, stack snapshots
   - Activated by triggers (crashes, specific functions, thresholds)
   - Pre-roll and post-roll windows around trigger events

### Component Architecture

1. **Tracer** (Rust): Control plane responsible for:
   - Shared memory management
   - Event collection and ATF writing
   - Leveraging tracer-backend to inject native agent into target

2. **Tracer-Backend** (C/C++): Backend to inject native agent into target:
   - Process spawning/attachment
   - Agent injection via Frida
   - Frida Gum hooks for function interception in the native agent
   - Hot-path event logging to shared memory
   - Minimal overhead instrumentation

3. **Query Engine** (Python): Analysis and search interface:
   - Token-budget-aware narratives
   - Symbol resolution and DWARF parsing
   - Structured filtering and search
   - Flight recorder window analysis

4. **MCP Server** (Python): Model Context Protocol interface:
   - Exposes tracing capabilities to AI agents
   - Manages trace sessions and queries

### Key Design Principles

- **Full Coverage by Default**: Hooks all resolvable functions without allowlists
- **Performance First**: Sustain 5M+ events/sec with <10% overhead
- **Crash Tolerant**: Best-effort flush on abnormal termination
- **Platform Aware**: Handles macOS security restrictions gracefully

### Non-Goals

- Don’t introduce top-level CMake as driver.  
- Don’t write artifacts outside target/.  
- Don’t expose unstable Rust internals.  

## MANDATORY: Code Quality

### Pre-commit Hooks

Install a pre-commit hook to check the test coverage of the increased codes such that we can guarantee the code quality is improved each time we commit.

Install a post-commit hook to generate an overall test coverage report to read at local.

### Quality Gate Requirements (100% Mandatory)

1. **Build Success**: 100% - ALL builds of all the components must pass
2. **Test Success**: 100% - ALL tests of all the components must pass
3. **Test Coverage**: 100% - NO coverage gaps allowed for the increased codes.
4. **Integration Score**: 100/100 - NO exceptions, NO compromises

### ⚠️ NO BYPASS MECHANISMS

- **NO ignoring tests** - Fix root cause instead
- **NO reducing requirements** - 100% is mandatory  
- **NO "temporary" quality compromises** - Quality is permanent
- **NO git commit --no-verify** - Hooks are mandatory
