#!/usr/bin/env python3
"""Test runner for the "use fast" high-performance subset (docs/design/use-fast.md).

Two kinds of test, discovered from *.ts / *.js in this directory:

- POSITIVE (default): the file must COMPILE and RUN with exit code 0. Write the
  test to self-check with assertions and `return failed > 0 ? 1 : 0` from
  user_main (like the node suite).

- NEGATIVE: a file whose header contains one or more
      // EXPECT-REJECT: <substring>
  lines must FAIL to compile, and the compiler's stderr must contain every
  listed <substring>. Used to prove FastCheck rejects out-of-subset code.
  Optionally `// EXPECT-OUTPUT: <substring>` (positive tests) asserts stdout.
"""
import re
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TESTS_DIR.parent))
from ts_test_platform import get_compiler_path, get_exe_suffix

COMPILER = get_compiler_path(TESTS_DIR.parent.parent)


def parse_directives(text):
    rejects, outputs = [], []
    for line in text.splitlines():
        if m := re.match(r'^\s*//\s*EXPECT-REJECT:\s*(.+?)\s*$', line):
            rejects.append(m.group(1))
        elif m := re.match(r'^\s*//\s*EXPECT-OUTPUT:\s*(.+?)\s*$', line):
            outputs.append(m.group(1))
    return rejects, outputs


def run_one(src):
    text = src.read_text(encoding='utf-8')
    rejects, outputs = parse_directives(text)
    tmp = Path(tempfile.gettempdir()) / f"fast_{uuid.uuid4().hex[:12]}"
    tmp.mkdir(parents=True, exist_ok=True)
    exe = tmp / (src.stem + get_exe_suffix())
    comp = subprocess.run([str(COMPILER), str(src), '-o', str(exe)],
                          capture_output=True, text=True, encoding='utf-8',
                          errors='replace', timeout=120)
    stderr = (comp.stderr or '') + (comp.stdout or '')

    if rejects:  # NEGATIVE test
        if comp.returncode == 0:
            return False, "expected compile failure but it succeeded"
        missing = [s for s in rejects if s not in stderr]
        if missing:
            return False, "compile failed but stderr missing: " + " | ".join(missing)
        return True, ""

    # POSITIVE test
    if comp.returncode != 0:
        return False, "compile failed: " + stderr.strip().splitlines()[-1] if stderr.strip() else "compile failed"
    run = subprocess.run([str(exe)], capture_output=True, text=True,
                         encoding='utf-8', errors='replace', timeout=60,
                         env={**__import__('os').environ,
                              'ICU_DATA': str(COMPILER.parent)})
    if run.returncode != 0:
        return False, f"run exited {run.returncode}: {(run.stdout or '').strip()}"
    for want in outputs:
        if want not in (run.stdout or ''):
            return False, f"stdout missing '{want}'"
    return True, ""


def main():
    if not COMPILER.exists():
        print(f"ERROR: compiler not found at {COMPILER}")
        return 1
    tests = sorted(list(TESTS_DIR.glob('*.ts')) + list(TESTS_DIR.glob('*.js')))
    passed = failed = 0
    print("=== use fast subset tests ===")
    for src in tests:
        ok, msg = run_one(src)
        if ok:
            passed += 1
            print(f"  PASS  {src.name}")
        else:
            failed += 1
            print(f"  FAIL  {src.name}: {msg}")
    print("-" * 40)
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
