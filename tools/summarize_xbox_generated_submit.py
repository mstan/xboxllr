"""Guest-anchored generated-submit CPU phases; inclusive child work, not GPU time.

Requires explicit cumulative endpoints and all twelve phase rows. These phases
partition the instrumented submit body, including nested witnessed flushes and
surface resolves. Do not add them to the exclusive emulation-thread partition.
"""
from __future__ import annotations

import argparse
import glob
import json
from pathlib import Path

from summarize_xbox_product_windows import window_summary

PHASES = ('entry', 'ensure', 'textures', 'records', 'buffer', 'uniforms',
          'state', 'draw', 'barrier', 'cleanup', 'error', 'finish')


def integer_delta(before: dict, after: dict, field: str) -> int:
    first, last = before[field], after[field]
    if type(first) is not int or type(last) is not int or not 0 <= first <= last:
        raise ValueError(f'invalid cumulative counter: {field}')
    return last - first


def summarize(report: dict, start: int, end: int) -> dict:
    capacity = window_summary(report, start, end)
    parsed = report['parsed']
    headers = {}
    phases = {}
    for row in parsed.get('generated_submit_cpu_samples', []):
        if row.get('scope') != 'cumulative' or row.get('deadline') not in (start, end):
            continue
        key = row['deadline']
        if key in headers:
            raise ValueError('duplicate generated-submit endpoint')
        if (row.get('inclusive_children') != 1 or row.get('available') != 1
                or row.get('enabled') != 1):
            raise ValueError('missing timing semantics')
        headers[key] = row
    for row in parsed.get('generated_submit_phase_samples', []):
        if row.get('scope') != 'cumulative' or row.get('deadline') not in (start, end):
            continue
        if row.get('phase') not in PHASES:
            raise ValueError('unknown generated-submit phase')
        key = (row['deadline'], row['phase'])
        if key in phases:
            raise ValueError('duplicate generated-submit phase endpoint')
        phases[key] = row
    if set(headers) != {start, end}:
        raise ValueError('missing generated-submit endpoint; census was absent or incomplete')
    before, after = headers[start], headers[end]
    discarded = integer_delta(before, after, 'discarded_deltas')
    if discarded:
        raise ValueError('discarded timing intervals inside requested window')
    rows = {}
    for phase in PHASES:
        if (start, phase) not in phases or (end, phase) not in phases:
            raise ValueError(f'missing phase endpoint: {phase}')
        first, last = phases[start, phase], phases[end, phase]
        raw = integer_delta(first, last, 'raw_ns')
        compensated = integer_delta(first, last, 'compensated_ns')
        calls = integer_delta(first, last, 'calls')
        if compensated > raw or (not calls and raw):
            raise ValueError(f'inconsistent phase counters: {phase}')
        rows[phase] = {'calls': calls, 'raw_seconds': raw / 1e9,
                       'compensated_seconds': compensated / 1e9,
                       'compensated_ns_per_call': compensated / calls if calls else None}
    return {
        'semantics': 'CPU submit body including nested child work; not exclusive GL or GPU time',
        'start': start, 'end': end, 'window_wall_seconds': capacity['wall_seconds'],
        'submits': integer_delta(before, after, 'submits'),
        'record_bytes': integer_delta(before, after, 'record_bytes'),
        'discarded_deltas': discarded,
        'raw_seconds': sum(row['raw_seconds'] for row in rows.values()),
        'compensated_seconds': sum(row['compensated_seconds'] for row in rows.values()),
        'phases': rows,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reports', nargs='+')
    parser.add_argument('--start', type=int, required=True)
    parser.add_argument('--end', type=int, required=True)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    paths = sorted({Path(p) for pattern in args.reports for p in glob.glob(pattern)})
    if not paths:
        parser.error('no matching reports')
    try:
        result = {str(path): summarize(json.loads(path.read_text(encoding='utf-8-sig')),
                                      args.start, args.end) for path in paths}
    except (ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    output = json.dumps(result, indent=2) + '\n'
    if args.json:
        args.json.write_text(output, encoding='utf-8')
    print(output, end='')


if __name__ == '__main__':
    main()
