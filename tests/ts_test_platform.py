"""Shared platform detection for ts-aot test runners.

Provides cross-platform paths for the compiler and compiled test outputs.
"""

import sys
from pathlib import Path


def get_project_root() -> Path:
    """Return the project root directory."""
    return Path(__file__).parent.parent


def get_compiler_path(root: Path = None) -> Path:
    """Return the path to the ts-aot compiler executable."""
    if root is None:
        root = get_project_root()
    if sys.platform == 'win32':
        return root / 'build' / 'src' / 'compiler' / 'Release' / 'ts-aot.exe'
    else:
        return root / 'build' / 'src' / 'compiler' / 'ts-aot'


def get_exe_suffix() -> str:
    """Return the executable suffix for the current platform."""
    return '.exe' if sys.platform == 'win32' else ''


def output_path_for(source: Path) -> Path:
    """Return the compiled output path for a given source file."""
    return source.with_suffix(get_exe_suffix())
