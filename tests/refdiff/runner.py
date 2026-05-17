#!/usr/bin/env python3
"""Reference-diff harness: ts-aot vs Node.js.

For each corpus/*.js script:
  1. Run via node, capture stdout (cached to corpus/*.expected).
  2. Compile + run via ts-aot, capture stdout.
  3. Diff. Any difference is a candidate bug.

Usage:
    python tests/refdiff/runner.py                # full corpus
    python tests/refdiff/runner.py -v             # show matches too
    python tests/refdiff/runner.py -k closure     # name filter
    python tests/refdiff/runner.py --bless        # accept current ts-aot output
                                                  # as expected (use carefully)
    python tests/refdiff/runner.py -j 8           # parallel

A "diff" is `ts-aot.stdout != node.stdout`. The runner ignores ts-aot's
own stderr unless ts-aot crashed.
"""

import argparse
import concurrent.futures as cf
import difflib
import hashlib
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
CORPUS_DIR = SCRIPT_DIR / "corpus"
PROJECT_ROOT = SCRIPT_DIR.parent.parent
COMPILER = PROJECT_ROOT / "build" / "src" / "compiler" / "Release" / "ts-aot.exe"
BUILD_DIR = SCRIPT_DIR / "build"

GREEN, RED, YELLOW, CYAN, RESET = "\033[32m", "\033[31m", "\033[33m", "\033[36m", "\033[0m"


def _hashed_exe_name(script: Path) -> str:
    """Hashed name to bypass Windows UAC installer-detection (WinError 740)
    when scripts contain words like 'install', 'delete', 'update'."""
    rel = script.relative_to(CORPUS_DIR).as_posix()
    h = hashlib.sha1(rel.encode("utf-8")).hexdigest()[:16]
    return ("r" + h + ".exe") if sys.platform == "win32" else ("r" + h)


@dataclass
class DiffResult:
    name: str
    status: str   # match / diff / crash / compile_error / no_node / timeout
    diff: str = ""
    reason: str = ""


def _normalize(s: str) -> str:
    """Strip trailing whitespace, normalize line endings."""
    return "\n".join(line.rstrip() for line in s.replace("\r\n", "\n").splitlines())


def _run_node(script: Path, timeout: int) -> tuple[str, str, int]:
    cp = subprocess.run(
        ["node", str(script)],
        capture_output=True, timeout=timeout, text=True,
        cwd=str(PROJECT_ROOT),
    )
    return cp.stdout or "", cp.stderr or "", cp.returncode


def _run_ts_aot(script: Path, timeout: int) -> tuple[str, str, int]:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    exe = BUILD_DIR / _hashed_exe_name(script)
    cp = subprocess.run(
        [str(COMPILER), str(script), "-o", str(exe)],
        capture_output=True, timeout=timeout, text=True,
        cwd=str(PROJECT_ROOT),
    )
    if cp.returncode != 0:
        return "", cp.stderr or "", -1  # signal compile_error
    rp = subprocess.run(
        [str(exe)], capture_output=True, timeout=timeout, text=True,
        cwd=str(PROJECT_ROOT),
    )
    return rp.stdout or "", rp.stderr or "", rp.returncode


def diff_script(script: Path, timeout: int = 5, bless: bool = False) -> DiffResult:
    name = script.stem
    expected_path = script.with_suffix(".expected")

    # 1. Get / cache node output
    if expected_path.exists() and not bless:
        node_out = _normalize(expected_path.read_text(encoding="utf-8"))
    else:
        try:
            node_stdout, node_stderr, node_rc = _run_node(script, timeout)
        except subprocess.TimeoutExpired:
            return DiffResult(name, "no_node", reason="node timeout")
        except FileNotFoundError:
            return DiffResult(name, "no_node", reason="node not on PATH")
        if node_rc != 0:
            return DiffResult(name, "no_node",
                              reason=f"node exited {node_rc}: {node_stderr[:100]}")
        node_out = _normalize(node_stdout)
        if not bless:
            expected_path.write_text(node_out + ("\n" if node_out else ""),
                                     encoding="utf-8")

    # 2. Run via ts-aot
    try:
        aot_stdout, aot_stderr, aot_rc = _run_ts_aot(script, timeout)
    except subprocess.TimeoutExpired:
        return DiffResult(name, "timeout", reason="ts-aot timeout")
    if aot_rc == -1:
        # Compile error
        err = (aot_stderr or "").strip().splitlines()
        msg = next((ln for ln in err if "[error]" in ln or "Error:" in ln),
                   (err[-1] if err else "compile failed"))
        return DiffResult(name, "compile_error", reason=msg[:200])
    if "FATAL: Uncaught" in aot_stderr or "VectoredException" in aot_stderr:
        # Extract message line
        first = next((ln for ln in aot_stderr.splitlines()
                      if ".message" in ln or "FATAL" in ln), aot_stderr[:160])
        return DiffResult(name, "crash", reason=first.strip()[:200])
    if aot_rc != 0:
        return DiffResult(name, "crash",
                          reason=f"ts-aot exited {aot_rc}: {aot_stderr[:160]}")

    aot_out = _normalize(aot_stdout)

    # 3. Compare
    if bless:
        expected_path.write_text(aot_out + ("\n" if aot_out else ""),
                                 encoding="utf-8")
        return DiffResult(name, "match", reason="blessed ts-aot output as expected")

    if aot_out == node_out:
        return DiffResult(name, "match")

    diff_lines = list(difflib.unified_diff(
        node_out.splitlines(),
        aot_out.splitlines(),
        fromfile=f"{name}.node",
        tofile=f"{name}.ts-aot",
        lineterm="",
    ))
    diff_text = "\n".join(diff_lines[:40])
    if len(diff_lines) > 40:
        diff_text += f"\n... ({len(diff_lines) - 40} more diff lines)"
    return DiffResult(name, "diff", diff=diff_text)


def main() -> int:
    parser = argparse.ArgumentParser(description="Reference-diff: ts-aot vs node")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show matching scripts too")
    parser.add_argument("-k", "--filter",
                        help="Substring filter on script filename")
    parser.add_argument("-j", "--jobs", type=int, default=4,
                        help="Parallel jobs (default: 4)")
    parser.add_argument("-t", "--timeout", type=int, default=5,
                        help="Per-script timeout in seconds (default: 5)")
    parser.add_argument("--bless", action="store_true",
                        help="Overwrite all .expected files with current ts-aot "
                             "output. Use only when you've manually verified the "
                             "current diff is the correct behavior.")
    args = parser.parse_args()

    if not COMPILER.exists():
        print(f"{RED}Error:{RESET} ts-aot not built at {COMPILER}", file=sys.stderr)
        return 1
    if not shutil.which("node"):
        print(f"{RED}Error:{RESET} node not on PATH (refdiff needs it as a reference)", file=sys.stderr)
        return 1
    if not CORPUS_DIR.exists():
        print(f"{YELLOW}Warning:{RESET} no corpus directory at {CORPUS_DIR}")
        return 0

    scripts = sorted(CORPUS_DIR.glob("*.js"))
    if args.filter:
        scripts = [s for s in scripts if args.filter in s.name]
    if not scripts:
        print("No scripts in corpus.")
        return 0

    print(f"Diffing {len(scripts)} script(s) (j={args.jobs}, ts-aot vs node)...")

    results = []
    if args.jobs > 1:
        with cf.ProcessPoolExecutor(max_workers=args.jobs) as ex:
            futs = {ex.submit(diff_script, s, args.timeout, args.bless): s for s in scripts}
            for fut in cf.as_completed(futs):
                results.append(fut.result())
    else:
        for s in scripts:
            results.append(diff_script(s, args.timeout, args.bless))

    results.sort(key=lambda r: r.name)

    counts = {"match": 0, "diff": 0, "crash": 0, "compile_error": 0,
              "timeout": 0, "no_node": 0}
    for r in results:
        counts[r.status] = counts.get(r.status, 0) + 1
        sym = {"match": GREEN + "." + RESET,
               "diff": RED + "D" + RESET,
               "crash": RED + "X" + RESET,
               "compile_error": RED + "C" + RESET,
               "timeout": YELLOW + "T" + RESET,
               "no_node": YELLOW + "?" + RESET}[r.status]
        if args.verbose or r.status != "match":
            line = f"  {sym} {r.name}"
            if r.reason:
                line += f"  {CYAN}{r.reason}{RESET}"
            print(line)
            if r.diff:
                for ln in r.diff.splitlines():
                    if ln.startswith("+"):
                        print(f"      {RED}{ln}{RESET}")
                    elif ln.startswith("-"):
                        print(f"      {GREEN}{ln}{RESET}")
                    else:
                        print(f"      {ln}")

    print()
    print("=" * 60)
    print(f"  Match:          {GREEN}{counts['match']:>4}{RESET}")
    print(f"  Diff:           {RED}{counts['diff']:>4}{RESET}")
    print(f"  Crash:          {RED}{counts['crash']:>4}{RESET}")
    print(f"  Compile error:  {RED}{counts['compile_error']:>4}{RESET}")
    print(f"  Timeout:        {YELLOW}{counts['timeout']:>4}{RESET}")
    print(f"  No-node skip:   {YELLOW}{counts['no_node']:>4}{RESET}")
    print(f"  Total:          {len(results):>4}")
    print("=" * 60)

    return 0 if counts["diff"] + counts["crash"] + counts["compile_error"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
