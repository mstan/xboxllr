"""Report same-population vertex/draw overlap, not predicted GPU savings."""
from __future__ import annotations

import argparse
import glob
import json
import math
from pathlib import Path

from summarize_xbox_generated_submit import integer_delta
from summarize_xbox_product_windows import window_summary

OUTCOMES = ('unknown', 'cpu_witnessed', 'gpu_witnessed', 'witnessed_empty',
            'generated_gpu', 'generated_empty', 'generic_resident_gpu',
            'generic_snapshot_gpu', 'generic_empty', 'generic_cpu', 'no_raster', 'fault')
COUNTERS = (
    'batches', 'submitted_vertices', 'transformed_vertices', 'executed_vsh_vertices',
    'index_cache_hits', 'transform_mode_other_batches', 'fixed_function_batches',
    'programmable_batches', 'interpreted_batches', 'compiled_batches',
    'context_writing_batches', 'generated_setup_calls', 'generated_setup_raw_ticks',
    'generated_setup_compensated_ticks', 'discarded_deltas', 'primitive_input_triangles',
    'primitive_collected_triangles',
    'primitive_refused_batches', 'primitive_nonfinite_batches',
    'fixture_setup_calls', 'fixture_setup_raw_ticks', 'fixture_setup_compensated_ticks',
    'fixture_input_vertices', 'fixture_output_vertices',
) + tuple('outcome_' + name for name in OUTCOMES) + tuple(f'mode_{i}' for i in range(4))
IDENTITY = ('kind', 'index', 'known', 'vsh_fp', 'start_slot', 'slots', 'outcome',
            'transform_mode', 'fixed_function', 'programmable', 'compiled', 'writes_context', 'fixture')


def checked_counts(row: dict) -> dict:
    result = {}
    for field in COUNTERS:
        value = row[field]
        if type(value) is not int or value < 0:
            raise ValueError(f'invalid counter {field}')
        result[field] = value
    if sum(result['outcome_' + name] for name in OUTCOMES) != result['batches']:
        raise ValueError('outcome counts do not conserve batches')
    if sum(result[f'mode_{i}'] for i in range(4)) + result['transform_mode_other_batches'] != result['batches']:
        raise ValueError('transform mode counts do not conserve batches')
    if result['compiled_batches'] + result['interpreted_batches'] != result['programmable_batches']:
        raise ValueError('compiled/interpreted counts do not conserve programmable batches')
    for phase in ('generated_setup', 'fixture_setup'):
        if result[phase + '_compensated_ticks'] > result[phase + '_raw_ticks']:
            raise ValueError('compensated time exceeds raw time')
        if not result[phase + '_calls'] and result[phase + '_raw_ticks']:
            raise ValueError('setup time without calls')
    if result['executed_vsh_vertices'] > result['transformed_vertices']:
        raise ValueError('VSH executions exceed transformed vertices')
    return result


def endpoint(parsed: dict, deadline: int) -> tuple[dict, dict]:
    headers = [r for r in parsed.get('vsh_draw_overlap_samples', [])
               if r.get('scope') == 'cumulative' and r.get('deadline') == deadline]
    if len(headers) != 1:
        raise ValueError('missing or duplicate overlap endpoint')
    header = headers[0]
    if header.get('schema') != 1 or header.get('enabled') != 1 or header.get('inclusive_setup') != 1:
        raise ValueError('missing overlap semantics')
    scale = header['clock_ns_per_tick']
    if not isinstance(scale, (float, int)) or not math.isfinite(scale) or scale <= 0:
        raise ValueError('invalid clock scale')
    checked_counts(header)
    capacity, count = header['row_capacity'], header['rows']
    if type(capacity) is not int or type(count) is not int or not 0 <= count <= capacity:
        raise ValueError('invalid row count')
    rows = {}
    for row in parsed.get('vsh_draw_overlap_rows', []):
        if row.get('scope') != 'cumulative' or row.get('deadline') != deadline:
            continue
        index = row['index']
        if type(index) is not int or index in rows:
            raise ValueError('duplicate or invalid row index')
        checked_counts(row)
        if row['kind'] != ('overflow' if index == capacity else 'program'):
            raise ValueError('invalid row kind')
        if row['kind'] == 'program':
            if row['outcome'] not in OUTCOMES:
                raise ValueError('unknown outcome name')
            if row['outcome_' + row['outcome']] != row['batches']:
                raise ValueError('row mixes outcome populations')
        rows[index] = row
    if set(rows) != set(range(count)) | {capacity}:
        raise ValueError('missing or unexpected overlap rows')
    for field in COUNTERS:
        if sum(row[field] for row in rows.values()) != header[field]:
            raise ValueError(f'rows do not conserve total {field}')
    return header, rows


def summarize(report: dict, start: int, end: int) -> dict:
    window = window_summary(report, start, end)
    before, first_rows = endpoint(report['parsed'], start)
    after, last_rows = endpoint(report['parsed'], end)
    if (before['row_capacity'] != after['row_capacity'] or before['rows'] > after['rows']
            or before['clock_ns_per_tick'] != after['clock_ns_per_tick']):
        raise ValueError('census or clock changed inside window')
    totals = {field: integer_delta(before, after, field) for field in COUNTERS}
    checked_counts(totals)
    if totals['discarded_deltas'] or totals['outcome_unknown'] or totals['outcome_fault']:
        raise ValueError('discarded timing, unresolved outcome, or fault inside window')
    scale = after['clock_ns_per_tick']
    zero = dict.fromkeys(COUNTERS, 0)
    rows = []
    routes = {name: dict(zero) for name in OUTCOMES}
    for index, last in last_rows.items():
        first = first_rows.get(index)
        if first and any(first[key] != last[key] for key in IDENTITY):
            raise ValueError('row identity changed inside window')
        delta = {field: integer_delta(first or zero, last, field) for field in COUNTERS}
        checked_counts(delta)
        if last['kind'] == 'overflow':
            if delta['batches']:
                raise ValueError('row overflow prevents exact route attribution')
            continue
        if not delta['batches']:
            continue
        row = {key: last[key] for key in IDENTITY}
        row.update(delta)
        row['setup_seconds'] = delta['generated_setup_compensated_ticks'] * scale / 1e9
        row['fixture_setup_seconds'] = delta['fixture_setup_compensated_ticks'] * scale / 1e9
        rows.append(row)
        for field in COUNTERS:
            routes[last['outcome']][field] += delta[field]
    for route in routes.values():
        route['setup_seconds'] = route['generated_setup_compensated_ticks'] * scale / 1e9
        route['fixture_setup_seconds'] = route['fixture_setup_compensated_ticks'] * scale / 1e9
        route['executed_vsh_share_pct'] = (100 * route['executed_vsh_vertices'] /
            totals['executed_vsh_vertices'] if totals['executed_vsh_vertices'] else None)
    rows.sort(key=lambda row: row['executed_vsh_vertices'], reverse=True)
    return {
        'semantics': 'Observed vertex/draw overlap with separate inclusive generated-collection and fixture-preparation CPU times; not predicted savings',
        'start': start, 'end': end, 'window_wall_seconds': window['wall_seconds'],
        'totals': totals, 'routes': routes, 'populations': rows,
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
