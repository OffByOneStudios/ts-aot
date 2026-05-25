#!/usr/bin/env python3
"""Fetch lodash 4.17.21's official test suite for the ts-aot upstream harness.

The npm `lodash` registry tarball strips `test/`, so test.js must come from
the git tag. QUnit comes from npm. Neither is vendored into this repo — this
script pulls them on demand into this directory (both gitignored).

Produces, in tests/lodash/upstream/:
  test.js     <- lodash 4.17.21 git tag test/test.js
  lodash.js   <- copy of tests/lodash/lodash.js (the 4.17.21 bundle under test)

Then run:
  build/src/compiler/Release/ts-aot.exe tests/lodash/upstream/harness.js -o tmp/lodash_upstream.exe
  tmp/lodash_upstream.exe        # prints LODASH-QUNIT PASS/FAIL/TOTAL

Node reference baseline (sanity-check the shim):
  node tests/lodash/upstream/harness.js   # expect ~6790/6794
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TAG = "4.17.21"
GIT_URL = "https://github.com/lodash/lodash.git"


def main():
    test_js = os.path.join(HERE, "test.js")
    lodash_js_dst = os.path.join(HERE, "lodash.js")
    lodash_js_src = os.path.join(HERE, "..", "lodash.js")

    if not os.path.exists(lodash_js_src):
        print("ERROR: tests/lodash/lodash.js (the bundle under test) not found.", file=sys.stderr)
        sys.exit(2)
    shutil.copyfile(lodash_js_src, lodash_js_dst)
    print("copied lodash.js bundle")

    if os.path.exists(test_js):
        print("test.js already present; delete it to re-fetch")
        return

    with tempfile.TemporaryDirectory() as tmp:
        clone = os.path.join(tmp, "lodash-src")
        print(f"shallow-cloning lodash {TAG} ...")
        r = subprocess.run(
            ["git", "clone", "--depth", "1", "--branch", TAG, GIT_URL, clone],
            capture_output=True, text=True,
        )
        if r.returncode != 0:
            print("git clone failed:\n" + r.stderr, file=sys.stderr)
            sys.exit(1)
        src = os.path.join(clone, "test", "test.js")
        if not os.path.exists(src):
            print("ERROR: test/test.js not found in clone", file=sys.stderr)
            sys.exit(1)
        shutil.copyfile(src, test_js)
        print(f"fetched test.js -> {test_js}")


if __name__ == "__main__":
    main()
