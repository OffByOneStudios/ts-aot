#!/usr/bin/env python3
"""Parse LLVM profraw v9 files and produce aggregate coverage reports.

Used by run_server_tests.py --coverage to merge per-test profraw data
and report which functions were exercised across all Express server tests.
"""

import struct
import sys
import zlib
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# profraw v9 header: 14 x uint64 = 112 bytes
HEADER_SIZE = 14 * 8
HEADER_FMT = '<14Q'
PROFRAW_MAGIC = 0xFF6C70726F667281
DATA_RECORD_SIZE = 64


def parse_profraw(path: Path) -> Optional[Dict]:
    """Parse a profraw v9 file. Returns dict with function counters.

    Returns:
        {
            'num_functions': int,
            'num_counters': int,
            'functions': {func_name_hash: {'hash': int, 'count': int}, ...}
        }
        or None on error.
    """
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        return None

    fields = struct.unpack_from(HEADER_FMT, data, 0)
    magic = fields[0]
    version = fields[1]
    num_data = fields[3]
    num_counters = fields[5]
    names_size = fields[9]

    if magic != PROFRAW_MAGIC:
        return None
    if version != 9:
        return None

    # Data records start after header
    data_offset = HEADER_SIZE
    counters_offset = data_offset + num_data * DATA_RECORD_SIZE

    if counters_offset + num_counters * 8 > len(data):
        return None

    # Parse data records to get function name hashes and counter assignments
    functions = {}
    counter_idx = 0
    for i in range(num_data):
        rec_offset = data_offset + i * DATA_RECORD_SIZE
        name_ref = struct.unpack_from('<Q', data, rec_offset)[0]
        func_hash = struct.unpack_from('<q', data, rec_offset + 8)[0]
        # NumCounters is at offset 48 in the record (after 6 x 8-byte fields)
        num_func_counters = struct.unpack_from('<I', data, rec_offset + 48)[0]

        # Sum all counters for this function
        total = 0
        for j in range(num_func_counters):
            if counter_idx + j < num_counters:
                val = struct.unpack_from('<Q', data, counters_offset + (counter_idx + j) * 8)[0]
                total += val

        functions[name_ref] = {
            'hash': func_hash,
            'count': total,
        }
        counter_idx += num_func_counters

    # Try to parse names section for human-readable function names
    names_offset = counters_offset + num_counters * 8
    # Skip padding after counters
    pad_after_counters = fields[6]  # PaddingBytesAfterCounters
    names_offset += pad_after_counters
    # Skip bitmap section
    num_bitmap_bytes = fields[7]
    pad_after_bitmap = fields[8]
    names_offset += num_bitmap_bytes + pad_after_bitmap

    name_map = _parse_names(data, names_offset, names_size)
    if name_map:
        for name_ref, info in functions.items():
            if name_ref in name_map:
                info['name'] = name_map[name_ref]

    return {
        'num_functions': num_data,
        'num_counters': num_counters,
        'functions': functions,
    }


def _parse_names(data: bytes, offset: int, size: int) -> Dict[int, str]:
    """Parse the compressed names section. Returns {name_hash: name_string}."""
    if offset + size > len(data) or size == 0:
        return {}

    raw = data[offset:offset + size]

    # Names section format: uncompressed_len (varint) + compressed_len (varint) + zlib data
    # Or just raw strings separated by \x01
    # Try zlib decompression first
    try:
        # Skip first byte (header marker) and try to find zlib stream
        for start in range(min(8, len(raw))):
            if raw[start:start + 1] == b'\x78':  # zlib magic
                decompressed = zlib.decompress(raw[start:])
                return _hash_names(decompressed)
    except (zlib.error, IndexError):
        pass

    # Try raw format: names separated by \x01
    try:
        names = raw.split(b'\x01')
        result = {}
        for name in names:
            name_str = name.decode('utf-8', errors='replace').strip('\x00')
            if name_str:
                h = _md5_hash(name_str)
                result[h] = name_str
        if result:
            return result
    except Exception:
        pass

    return {}


def _hash_names(names_data: bytes) -> Dict[int, str]:
    """Hash individual function names from decompressed data."""
    names = names_data.split(b'\x01')
    result = {}
    for name in names:
        name_str = name.decode('utf-8', errors='replace').strip('\x00')
        if name_str:
            h = _md5_hash(name_str)
            result[h] = name_str
    return result


def _md5_hash(name: str) -> int:
    """Compute LLVM's function name hash (lower 64 bits of MD5)."""
    import hashlib
    digest = hashlib.md5(name.encode('utf-8')).digest()
    return struct.unpack('<Q', digest[:8])[0]


def merge_coverage(datasets: List[Tuple[str, Dict]]) -> Dict:
    """Merge coverage from multiple profraw parses.

    Each dataset is (test_name, parse_profraw_result).
    Returns merged dict with per-function max counts.
    """
    merged_functions: Dict[int, Dict] = {}

    for test_name, dataset in datasets:
        for name_ref, info in dataset['functions'].items():
            if name_ref not in merged_functions:
                merged_functions[name_ref] = {
                    'hash': info['hash'],
                    'count': info['count'],
                    'name': info.get('name', ''),
                    'tests': [],
                }
            else:
                # Take max count (function may be counted in multiple tests)
                existing = merged_functions[name_ref]
                existing['count'] = max(existing['count'], info['count'])
                if not existing['name'] and info.get('name'):
                    existing['name'] = info['name']
            if info['count'] > 0:
                merged_functions[name_ref]['tests'].append(test_name)

    total = len(merged_functions)
    hit = sum(1 for f in merged_functions.values() if f['count'] > 0)

    return {
        'total_functions': total,
        'hit_functions': hit,
        'functions': merged_functions,
    }


def print_report(merged: Dict, total_profraw: int, parsed_profraw: int):
    """Print a coverage summary report."""
    total = merged['total_functions']
    hit = merged['hit_functions']
    missed = total - hit
    pct = (100.0 * hit / total) if total > 0 else 0.0

    print(f"\n{'=' * 50}")
    print(f"Express Coverage Report ({parsed_profraw}/{total_profraw} tests with profraw)")
    print(f"{'=' * 50}")
    print(f"Functions tracked: {total}")
    print(f"Functions hit:     {hit} ({pct:.1f}%)")
    print(f"Functions missed:  {missed} ({100.0 - pct:.1f}%)")

    # Show top uncovered functions (with names if available)
    functions = merged['functions']
    named_missed = [
        f for f in functions.values()
        if f['count'] == 0 and f.get('name')
    ]
    named_hit = [
        f for f in functions.values()
        if f['count'] > 0 and f.get('name')
    ]

    if named_hit:
        # Group by module (functions often have module prefix)
        print(f"\nHit functions with names: {len(named_hit)}")
        for f in sorted(named_hit, key=lambda x: -x['count'])[:20]:
            tests_str = f", tests: {', '.join(f['tests'][:3])}" if f.get('tests') else ''
            print(f"  {f['name']}: count={f['count']}{tests_str}")
        if len(named_hit) > 20:
            print(f"  ... and {len(named_hit) - 20} more")

    if named_missed:
        print(f"\nMissed functions with names: {len(named_missed)}")
        for f in sorted(named_missed, key=lambda x: x.get('name', ''))[:20]:
            print(f"  {f['name']}")
        if len(named_missed) > 20:
            print(f"  ... and {len(named_missed) - 20} more")


def main():
    """Standalone usage: parse and report on profraw files."""
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <profraw_file> [profraw_file ...]")
        print(f"       {sys.argv[0]} <directory>  (parses all .profraw in dir)")
        return 1

    paths = []
    for arg in sys.argv[1:]:
        p = Path(arg)
        if p.is_dir():
            paths.extend(sorted(p.glob('*.profraw')))
        elif p.exists():
            paths.append(p)
        else:
            print(f"Warning: {arg} not found", file=sys.stderr)

    if not paths:
        print("No profraw files found.")
        return 1

    datasets = []
    for p in paths:
        data = parse_profraw(p)
        if data:
            datasets.append((p.stem, data))
            print(f"Parsed {p.name}: {data['num_functions']} functions, "
                  f"{data['num_counters']} counters")
        else:
            print(f"Warning: could not parse {p.name}", file=sys.stderr)

    if datasets:
        merged = merge_coverage(datasets)
        print_report(merged, len(paths), len(datasets))

    return 0


if __name__ == '__main__':
    sys.exit(main())
