# Junction-safe deletion of agent worktrees under .claude/worktrees.
#
# NEVER use `git worktree remove --force` here: it follows junctions and has
# deleted the shared test262 corpus before (see memory
# parallel-worktree-agent-harvest-2026-07-18, GOTCHA 2). This script instead:
#   1. unlinks every reparse point (junction/symlink) inside each tree —
#      DirectoryInfo/FileInfo.Delete() on a reparse point removes ONLY the
#      link, never the target's contents;
#   2. recursively deletes the then-junction-free tree;
#   3. tripwires on the shared test262 corpus after every tree and aborts
#      if it ever disappears.
# Finish with `git worktree prune` (this script runs it at the end).
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts/clean_worktrees.ps1
#         (add -Only <name-substring> to limit which trees are deleted)
param([string]$Only = '')

$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
$root = Join-Path $repo '.claude\worktrees'
$corpus = Join-Path $repo 'tests\test262\test262\test'

if (-not (Test-Path -LiteralPath $root)) { Write-Host 'no worktrees dir'; exit 0 }
if (-not (Test-Path -LiteralPath $corpus)) { Write-Host 'ABORT: corpus missing before start'; exit 1 }

$trees = Get-ChildItem -LiteralPath $root -Directory
if ($Only) { $trees = $trees | Where-Object { $_.Name -like ('*' + $Only + '*') } }
Write-Host ('worktrees to delete: ' + $trees.Count)
$i = 0
foreach ($wt in $trees) {
    $i++
    $links = Get-ChildItem -LiteralPath $wt.FullName -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue
    foreach ($l in $links) {
        try { $l.Delete() } catch { Write-Host ('  link del fail: ' + $l.FullName) }
    }
    Remove-Item -LiteralPath $wt.FullName -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $wt.FullName) { Write-Host ('  RESIDUE (locked?): ' + $wt.Name) }
    if (($i % 10) -eq 0) { Write-Host ('done ' + $i + '/' + $trees.Count) }
    if (-not (Test-Path -LiteralPath $corpus)) { Write-Host 'ABORT: CORPUS VANISHED'; exit 2 }
}
Write-Host 'corpus intact:' (Test-Path -LiteralPath $corpus)
git -C $repo worktree prune
Write-Host 'CLEANUP DONE'
