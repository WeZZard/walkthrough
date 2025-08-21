#!/usr/bin/env python3
"""
Merge multiple compile_commands.json files from different components.

This is useful when you have multiple C/C++ components in a Cargo workspace,
each generating their own compile_commands.json file.
"""

import json
import sys
from pathlib import Path
from typing import List, Dict, Any


def find_compile_commands(root_dir: Path) -> List[Path]:
    """Find all compile_commands.json files under the target directory."""
    compile_commands_files = []
    
    # Look in target directory
    target_dir = root_dir / "target"
    if target_dir.exists():
        compile_commands_files.extend(target_dir.rglob("compile_commands.json"))
    
    return compile_commands_files


def merge_compile_commands(files: List[Path]) -> List[Dict[str, Any]]:
    """Merge multiple compile_commands.json files into a single list."""
    all_commands = []
    seen_files = set()
    
    for file_path in files:
        try:
            with open(file_path, 'r') as f:
                commands = json.load(f)
                
            for cmd in commands:
                # Avoid duplicates based on file path
                file_key = cmd.get('file', '')
                if file_key and file_key not in seen_files:
                    all_commands.append(cmd)
                    seen_files.add(file_key)
                    
        except (json.JSONDecodeError, IOError) as e:
            print(f"Warning: Failed to read {file_path}: {e}", file=sys.stderr)
    
    return all_commands


def main():
    """Main entry point."""
    # Get project root (parent of utils directory)
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    
    # Find all compile_commands.json files
    compile_files = find_compile_commands(project_root)
    
    if not compile_files:
        print("No compile_commands.json files found under target/", file=sys.stderr)
        print("Run 'cargo build' first to generate them.", file=sys.stderr)
        sys.exit(1)
    
    print(f"Found {len(compile_files)} compile_commands.json file(s):", file=sys.stderr)
    for f in compile_files:
        print(f"  - {f.relative_to(project_root)}", file=sys.stderr)
    
    # Merge all commands
    merged_commands = merge_compile_commands(compile_files)
    
    # Write merged file to target directory
    output_path = project_root / "target" / "compile_commands.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w') as f:
        json.dump(merged_commands, f, indent=2)
    
    print(f"\nMerged {len(merged_commands)} compilation commands", file=sys.stderr)
    print(f"Output written to: {output_path.relative_to(project_root)}", file=sys.stderr)
    
    # Also print absolute path for IDE configuration
    print(f"Absolute path: {output_path.absolute()}", file=sys.stderr)


if __name__ == "__main__":
    main()