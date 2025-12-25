---
id: BH-011
title: Symbol Resolution and DWARF Integration
status: active
source: docs/specs/QUERY_ENGINE_SPEC.md
---

# Symbol Resolution and DWARF Integration

## Context

**Given:**
- Function symbols may be mangled (Swift, C++, Rust)
- Debug symbols (DWARF/dSYM) provide rich metadata (signatures, parameters, types)
- Queries need to filter by function name, parameter name, type, and module
- dSYM files may not always be available
- Demangling improves readability and searchability
- Symbol resolution must be performant for large codebases

## Trigger

**When:** The Query Engine needs to resolve, demangle, or search function symbols

## Outcome

**Then:**
- Both mangled and demangled names are exposed
- Demangled names are normalized for search (e.g., whitespace, qualifiers)
- DWARF/dSYM indexes are built for function signatures and parameters
- Filters support: `functionPattern`, `paramNamePattern`, `typePattern`, `modulePattern`
- System falls back gracefully when dSYM is unavailable (name-based matching still works)
- Side indexes maintained: `functionId → {module, name_demangled, file:line, params[], returnType}`

## Demangling Support

### Supported Languages
- **Swift:** `_$s...` mangling scheme
- **C++:** Itanium ABI mangling (`_Z...`)
- **Rust:** Modern Rust mangling (`_R...`)

### Implementation
- macOS: Use system toolchain libraries (Swift, C++ demanglers)
- Linux: Use binutils or LLVM demangler
- Fallback: Keep mangled name if demangler unavailable

### Name Normalization
After demangling, normalize for search:
- Remove excessive whitespace
- Normalize template/generic parameters
- Preserve semantic differences (e.g., const, reference types)
- Store both raw demangled and normalized versions

## DWARF/dSYM Integration

### macOS Workflow
1. Match Mach-O UUID to dSYM bundle
2. Parse DWARF sections using `gimli` or `object` crate
3. Extract:
   - Function names (mangled and demangled)
   - Source file and line numbers
   - Parameter names and types
   - Return types
   - Inlined function locations

### Index Structure
```rust
struct SymbolIndex {
    function_id: u64,
    module: String,
    name_mangled: String,
    name_demangled: String,
    name_normalized: String,
    file: Option<String>,
    line: Option<u32>,
    params: Vec<Parameter>,
    return_type: Option<String>,
}

struct Parameter {
    name: String,
    type_name: String,
}
```

### Query Filters

#### functionPattern
- Match against demangled and normalized names
- Support regex patterns
- Case-insensitive option

#### paramNamePattern
- Match parameter names from DWARF
- Useful for finding functions that take specific arguments
- Example: Find all functions with a parameter named "userId"

#### typePattern
- Match parameter types and return types
- Support fully-qualified type names
- Example: Find all functions returning `Result<Image, Error>`

#### modulePattern
- Match module/library name
- Support wildcards
- Example: `libimage.*` matches all image processing libraries

## Edge Cases

### Missing dSYM
**Given:** A trace is analyzed but dSYM files are not available
**When:** Symbol resolution is performed
**Then:**
- Demangling still works (only mangled name needed)
- DWARF-based filters (parameters, types) are unavailable
- Name-based matching still functions
- Query results indicate "limited symbol info"
- System does not error; provides best available data

### Stripped Binaries
**Given:** The binary has been stripped of symbols
**When:** Symbol resolution is attempted
**Then:**
- Function IDs are still available (from module + symbol index)
- Addresses are used as fallback identifiers
- Offline symbolization may be possible with separate symbol files
- Query results show addresses instead of names

### Inlined Functions
**Given:** A function is inlined by the compiler
**When:** DWARF is parsed
**Then:**
- Inline entries are extracted from DWARF
- Inline instances are mapped to their call sites
- Each inline instance gets a unique identifier
- Queries can filter by source line even for inlined code

### Template/Generic Instantiations
**Given:** C++ templates or Swift generics create multiple instantiations
**When:** Demangling and indexing occur
**Then:**
- Each instantiation is indexed separately
- Normalized names group similar instantiations
- Queries can match generic patterns (e.g., `std::vector<*>`)
- Exact type matches are also supported

### Module Reloading
**Given:** A module is unloaded and reloaded with different symbols
**When:** Indexing occurs
**Then:**
- Module UUID differentiates versions
- Each version gets a distinct moduleId
- Symbol tables are versioned per module load
- Queries can filter by module version if needed

### Cross-Language Symbols
**Given:** A trace includes Swift, C++, and Rust code
**When:** Symbol resolution is performed
**Then:**
- Language-specific demanglers are applied
- Normalized names use common conventions where possible
- Language is recorded in metadata
- Queries can filter by language if needed

## Performance Considerations

### Index Caching
- DWARF parsing is expensive; cache results
- Invalidate cache on dSYM file change (checksum or mtime)
- Memory-map symbol index for large traces

### Lazy Loading
- Parse DWARF only for modules referenced in queries
- Build index incrementally as needed
- Keep hot paths (name lookup) fast

### Search Optimization
- Build inverted index: name → [functionIds]
- Support prefix/suffix trees for pattern matching
- Pre-compute normalized names for fast comparison

## Acceptance Criteria (MVP)

### A1: Demangling and DWARF filters
**Given:** Traces with dSYMs
**When:** Queries by demangled name, param name, and type are issued
**Then:**
- Queries match the expected functions
- Demangled names are human-readable
- Without dSYMs, name-only queries still work

## References

- Original: `docs/specs/QUERY_ENGINE_SPEC.md` (archived source - section 7)
- Related: `BH-010-query-api` (Query API)
- Related: `BH-012-narrative-generation` (Narrative Generation using symbols)
