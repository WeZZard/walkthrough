# CMake Compilation Database Setup

This document explains how the project automatically generates and manages `compile_commands.json` for IDE integration.

## Overview

The `compile_commands.json` file is a CMake compilation database that provides IDE tools with exact compiler commands for each source file. This enables:

- Accurate IntelliSense/code completion
- Proper error squiggles
- Go to definition/declaration
- Include path resolution
- Macro expansion

## Automatic Generation

The build system automatically generates `compile_commands.json` when you run `cargo build`:

1. **Cargo invokes build.rs** for each C/C++ component
2. **build.rs runs CMake** with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`
3. **CMake generates compile_commands.json** in the build directory
4. **build.rs creates a symlink** at `target/compile_commands.json` for easy access

## File Locations

After running `cargo build`, you'll find compile_commands.json at:

- **Generated location**: `target/debug/build/<component>-*/out/build/compile_commands.json`
- **Symlink location**: `target/compile_commands.json` (for IDE convenience)
- **Per-component**: Each C/C++ component generates its own file

## IDE Configuration

### VSCode

The project includes `.vscode/c_cpp_properties.json` pre-configured to use the symlink:

```json
{
    "configurations": [{
        "compileCommands": "${workspaceFolder}/target/compile_commands.json"
    }]
}
```

No additional configuration needed!

### CLion

1. Open Settings → Build, Execution, Deployment → CMake
2. Set "Generation path" to `target`
3. Or manually set compile commands path to `<project>/target/compile_commands.json`

### Neovim/Vim (with clangd)

Create `.clangd` in project root:
```yaml
CompileFlags:
  CompilationDatabase: target
```

Or configure your LSP client to look in the `target` directory.

### Emacs (with lsp-mode)

Add to your configuration:
```elisp
(setq lsp-clients-clangd-args
      '("--compile-commands-dir=target"))
```

## Multiple Components

When working with multiple C/C++ components, you may have multiple `compile_commands.json` files. Use the merge script:

```bash
python utils/merge_compile_commands.py
```

This will:
1. Find all compile_commands.json files under `target/`
2. Merge them into a single file
3. Write the result to `target/compile_commands.json`
4. Remove duplicates automatically

## Troubleshooting

### compile_commands.json not found

**Problem**: IDE can't find the compilation database

**Solution**:
1. Ensure you've run `cargo build` at least once
2. Check that the symlink exists at `target/compile_commands.json`
3. Verify your IDE is configured to look in the correct location

### Stale compilation commands

**Problem**: IDE shows errors for code that compiles fine

**Solution**:
```bash
# Clean and rebuild to regenerate compile_commands.json
cargo clean
cargo build
```

### Missing includes or wrong flags

**Problem**: IDE can't find headers or uses wrong compiler flags

**Solution**:
1. Check that CMakeLists.txt properly specifies include directories
2. Ensure all dependencies are initialized (`./utils/init_third_parties.sh`)
3. Rebuild to regenerate the compilation database

### Component-specific issues

If working on a specific component and the merged compile_commands.json doesn't work well:

1. Use the component-specific file directly:
   ```
   target/debug/build/tracer-backend-*/out/build/compile_commands.json
   ```

2. Or build only that component:
   ```bash
   cargo build -p tracer-backend
   ```

## Best Practices

1. **Always build through Cargo**: Never run CMake directly
2. **Commit .vscode/c_cpp_properties.json**: Share IDE config with team
3. **Don't commit compile_commands.json**: It's generated and user-specific
4. **Add to .gitignore**: Ensure `compile_commands.json` is ignored
5. **Document IDE setup**: Add component-specific notes to component READMEs

## Implementation Details

The compile_commands.json generation is implemented in:

- `tracer-backend/build.rs`: Adds CMAKE_EXPORT_COMPILE_COMMANDS=ON flag
- `tracer-backend/CMakeLists.txt`: Sets CMAKE_EXPORT_COMPILE_COMMANDS ON by default
- `utils/merge_compile_commands.py`: Merges multiple databases

The build system ensures that:
- Files are always written under `target/` (never in repo root)
- Symlinks are created for IDE convenience
- Paths are absolute for maximum compatibility
- The database is regenerated on each build