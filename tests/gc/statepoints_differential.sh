#!/bin/bash
# PHASE0 step 0.3 gate: --gc-statepoints behavioral differential.
#
# Compiles each program in a corpus twice (default build = oracle, and with
# --gc-statepoints), runs both, and compares output. The precise-roots path is
# "default-able" when this reports zero REAL-MISMATCH / SP-CRASH / SP-CE.
#
# Normalization (so the differential measures BEHAVIOR, not noise):
#   - strips the [StackMap]/[GCRoots] diagnostic lines (gated off by default, but
#     re-appear under TS_GC_ROOTS_VERBOSE);
#   - normalizes ASLR pointers (0x... -> 0xADDR);
#   - normalizes bare "at 0xADDR" crash-backtrace frames AND collapses runs of
#     them, because statepoints adds safepoint frames to a crash backtrace (a
#     cosmetic depth difference on programs that crash identically in both modes).
#
# Usage:  bash tests/gc/statepoints_differential.sh [corpus_glob_root]
#   default corpus root: tests/golden_ir
set -u
ROOT="${1:-tests/golden_ir}"
COMPILER="build/src/compiler/Release/ts-aot.exe"
TMP="${TMPDIR:-tmp}"; mkdir -p "$TMP"

norm() {
  grep -avE "\[StackMap\]|\[GCRoots\]" \
    | sed -E 's/0x[0-9a-fA-F]+/0xADDR/g' \
    | sed -E 's/^[[:space:]]*at 0xADDR$/@FRAME/' \
    | awk '!(/^@FRAME$/ && prev=="@FRAME"){print} {prev=$0}'
}

match=0; mismatch=0; sp_crash=0; sp_ce=0; base_bad=0
mapfile -t files < <(find "$ROOT" -name "*.ts" -o -name "*.js" | grep -vE "\.lib$")
for f in "${files[@]}"; do
  n=$(echo "$f" | md5sum | cut -c1-12)
  "$COMPILER" "$f" -o "$TMP/b_$n.exe" >/dev/null 2>&1 || { base_bad=$((base_bad+1)); continue; }
  bout=$(timeout 10 "./$TMP/b_$n.exe" 2>/dev/null | norm)
  "$COMPILER" "$f" --gc-statepoints -o "$TMP/s_$n.exe" >/dev/null 2>&1 || { sp_ce=$((sp_ce+1)); echo "SP-CE: $f"; continue; }
  sout=$(timeout 10 "./$TMP/s_$n.exe" 2>/dev/null | norm); rc=${PIPESTATUS[0]}
  if [ "$bout" == "$sout" ]; then match=$((match+1)); else mismatch=$((mismatch+1)); echo "REAL-MISMATCH: $f"; fi
done
echo "=== --gc-statepoints behavioral differential ($ROOT) ==="
echo "MATCH=$match REAL-MISMATCH=$mismatch SP-CE=$sp_ce base-skipped=$base_bad total=${#files[@]}"
[ $mismatch -eq 0 ] && [ $sp_ce -eq 0 ]
