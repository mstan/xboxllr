"""Bound the pinned donor's optional flat lookup without changing its targets.

The cluster/outlier approach follows phobos665/xboxrecomp 7bf9c188, reviewed
2026-10-18. This is a private GPL-dependent generated-pilot transformation;
it does not import the donor's kernel or change CPU/register lowering.
"""
from pathlib import Path
import re

GAP = 0x100000
MAX_BYTES = 32 * 1024 * 1024
COUNT = 'sizeof(g_recomp_table) / sizeof(g_recomp_table[0])'


def refresh_inventory_count(source):
    """Incremental function recovery must not retain the old numeric bound."""
    result, count = re.subn(r'(g_recomp_table_size\s*=\s*)[^;]+;',
                            lambda m: m[1] + COUNT + ';', source)
    if count != 1:
        raise ValueError('missing or ambiguous dispatch inventory bound')
    return result


def compact(source):
    table = re.search(r'g_recomp_table\[\]\s*=\s*\{(.*?)\n\};', source, re.S)
    if not table:
        raise ValueError('missing generated dispatch inventory')
    addresses = [int(x, 16) for x in re.findall(r'\{\s*0x([0-9A-Fa-f]+)u,', table[1])]
    if len([line for line in table[1].splitlines() if line.strip()]) != len(addresses):
        raise ValueError('unrecognized dispatch inventory row')
    count = re.search(r'g_recomp_table_size\s*=\s*([^;]+);', source)
    if not count or not addresses or (count[1] != COUNT and
            (not count[1].isdigit() or int(count[1]) != len(addresses))):
        raise ValueError('dispatch inventory count mismatch or empty inventory')
    if addresses != sorted(set(addresses)):
        raise ValueError('dispatch addresses must be unique and sorted')
    # Most entries wins; ties keep the lower cluster. Outliers remain valid.
    clusters = []
    first = 0
    for i in range(1, len(addresses) + 1):
        if i == len(addresses) or addresses[i] - addresses[i - 1] >= GAP:
            clusters.append((first, i))
            first = i
    first, end = max(clusters, key=lambda item: item[1] - item[0])
    base = addresses[first]
    span = addresses[end - 1] - base + 1
    start = source.find('/* ----------------------------------------------------------------\n * Flat, directly-indexed dispatch.')
    if start < 0:
        start = source.find('/* PILOT compact indirect dispatch.')
    finish = source.find('/* Get the number of registered functions */', start)
    if start < 0 or finish < 0:
        raise ValueError('unrecognized pinned dispatch implementation')
    replacement = f'''/* PILOT compact indirect dispatch. The sorted original inventory above
 * remains authoritative, including outliers and holes. Initialize once before
 * guest threads start. Allocation failure keeps the exact binary lookup.
 * No guest execution backend or manual/kernel override selection changes. */
#define PILOT_DISPATCH_COMPACT 1
static const uint32_t g_flat_base = 0x{base:08X}u;
static const uint32_t g_flat_span = 0x{span:08X}u;
static recomp_func_t *g_flat_table;

int recomp_dispatch_init(void)
{{
    if (g_flat_table) return 1;
    if ((uint64_t)g_flat_span * sizeof(recomp_func_t) > {MAX_BYTES}u) return 0;
    recomp_func_t *table = (recomp_func_t *)calloc(g_flat_span, sizeof(*table));
    if (!table) return 0;
    for (size_t i = {first}; i < {end}; ++i)
        table[g_recomp_table[i].xbox_va - g_flat_base] = g_recomp_table[i].func;
    g_flat_table = table;
    return 1;
}}

size_t recomp_dispatch_flat_bytes(void)
{{
    return g_flat_table ? (size_t)g_flat_span * sizeof(recomp_func_t) : 0;
}}

recomp_func_t recomp_lookup(uint32_t xbox_va)
{{
    uint32_t offset = xbox_va - g_flat_base;
    if (g_flat_table && offset < g_flat_span) return g_flat_table[offset];
    size_t low = 0, high = g_recomp_table_size;
    while (low < high) {{
        size_t mid = low + (high - low) / 2;
        uint32_t address = g_recomp_table[mid].xbox_va;
        if (address < xbox_va) low = mid + 1;
        else if (address > xbox_va) high = mid;
        else return g_recomp_table[mid].func;
    }}
    return NULL;
}}

'''
    record = dict(entries=len(addresses), flat_entries=end-first,
                  base=base, span=span, max_bytes=MAX_BYTES)
    result = source[:start] + replacement + source[finish:]
    return refresh_inventory_count(result), record


def apply(directory):
    path = Path(directory) / 'recomp_dispatch.c'
    source, record = compact(path.read_text(encoding='utf-8'))
    path.write_text(source, encoding='utf-8', newline='\n')
    return record
