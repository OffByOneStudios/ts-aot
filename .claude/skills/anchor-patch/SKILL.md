---
name: anchor-patch
description: Apply multi-site source edits via a Python anchor-patch script instead of many Edit calls. Use when a change touches 3+ sites, spans multiple files, repeats a pattern, or when Edit keeps failing on uniqueness/whitespace. Trigger terms - "patch script", "multi-site edit", "wire into N sites".
allowed-tools: Bash, Write, Read
---

# Anchor-Patch Scripts

Make coordinated source edits by writing a small Python script that replaces
exact-text anchors, with occurrence-count assertions, instead of issuing many
individual Edit tool calls.

## When to Use

- A change must land at 3+ sites (e.g. wiring a new helper into every
  iteration method in `TsArray_Methods.cpp`).
- The same textual pattern repeats and you want `replace_all` semantics WITH
  verification of how many sites matched.
- The edit spans multiple files that must change together (runtime + registry
  + compiler lowering — the classic 3-layer change in this repo).
- Edit keeps failing on uniqueness or you're fighting whitespace/indent
  mismatches.
- You want the patch to be reviewable/replayable as an artifact (save it to
  `tmp/<name>_patch.py`).

For 1–2 simple, unique edits, plain Edit is faster — don't script those.

## The Template

Write the script to `tmp/<topic>_patch.py` (survives for re-runs and
post-mortems), then `python tmp/<topic>_patch.py`:

```python
NL = chr(10)
def edit(path, old, new, count=1):
    src = open(path, encoding='utf-8').read()
    n = src.count(old)
    assert n == count, (path, n, old[:70])   # LOUD failure with context
    open(path, 'w', encoding='utf-8').write(src.replace(old, new))

R = r'E:\src\github.com\cgrinker\ts-aoc'

# 1. one edit() call per logical change, each printing progress
edit(R + r'\src\runtime\src\TsArray.cpp', """<exact anchor>""", """<replacement>""")
print('1 ok')
# ...
print('ALL OK')
```

Rules that make this reliable:

- **Anchor on exact multi-line text** copied from a fresh Read/sed of the
  file — never retype from memory. Include enough surrounding lines to be
  unique.
- **Always assert the count.** `count=1` for unique sites; when deliberately
  hitting N identical sites, pass `count=N`. A failed assert means the file
  drifted since you read it — re-read, don't force.
- **Replacement = anchor + insertion** for additive changes, so a re-run of
  the script fails the count assert instead of double-inserting.
- **Print a marker after each edit** (`print('3 ok')`) so a mid-script
  assert tells you exactly which edits landed before the failure. Edits
  before the failure ARE applied — fix the script's remaining anchors and
  re-run only what's left, or anchor the rerun on the already-patched text.

## Escaping Gotchas (learned the hard way)

- **Backslashes and `\n` inside C string literals**: a heredoc-quoted Python
  script that writes `"...\n"` into C++ source can end up writing a literal
  newline (C2001 "newline in constant") or `'\\'`→`'\'` (C2015). When a
  string must contain backslash escapes, build it with `chr(92)`/`chr(10)`:
  ```python
  bs = chr(92); nl = chr(10)
  old = '"[DBG] x=%d' + nl + '",'          # broken text currently in file
  new = '"[DBG] x=%d' + bs + 'n",'         # correct \n escape
  ```
- **Substring matches inside longer lines**: `str.replace` matches
  substrings. An 8-space-indented anchor will also match INSIDE a
  16-space-indented copy of the same statement and corrupt it (this produced
  a mid-line insertion once). When the same statement exists at multiple
  indents, include the leading indentation in the anchor and count BOTH
  variants first (`src.count(...)`) before replacing.
- **Heredocs**: use `python - <<'PYEOF'` (quoted delimiter, no shell
  expansion). If the heredoc itself errors ("unexpected EOF"), fall back to
  Write-ing the script to `tmp/` and running it — that's also the default
  for anything over ~40 lines.

## After the Patch

Grep-verify the result once (`grep -c <new symbol> <file>`), then build.
Compile errors after an anchor patch are usually one of the escaping gotchas
above — `sed -n <line>p` the reported line and fix with a follow-up
chr()-built replace, not by hand-editing (keeps the patch replayable).
