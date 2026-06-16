# Regenerate the ctags index for ts-aot.
#
# Indexes the C/C++ compiler + runtime + extensions. Crucially uses
# --kinds-C(++)=+p so function *prototypes* (e.g. the `extern "C"` runtime
# decls like ts_call / ts_value_make_int) are tagged, not just definitions.
#
# Query examples (Universal Ctags ships readtags):
#   readtags -t tags ts_call            # all defs/decls of ts_call
#   readtags -t tags -p ts_call         # prefix match
#   grep -P "^ts_call\b" tags           # plain-grep fallback
#
# Excludes build artifacts, vendored deps, and the test262 corpus (huge,
# not our code). The `tags` file itself is gitignored.

$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    ctags `
        --recurse=yes `
        --languages=C,C++ `
        --kinds-C=+p `
        --kinds-C++=+p `
        --fields=+iaSnKt `
        --extras=+q `
        --exclude=build `
        --exclude=node_modules `
        --exclude=.git `
        --exclude=tmp `
        --exclude=tests/test262/test262 `
        --exclude='*.min.js' `
        -f tags `
        src extensions
    $count = (Get-Content tags | Measure-Object -Line).Lines
    Write-Host "Tags updated: $count entries in $repo\tags"
}
finally {
    Pop-Location
}
