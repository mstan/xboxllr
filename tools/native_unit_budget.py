#!/usr/bin/env python3
"""Partition numeric generated C units without changing any function bytes."""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import os
import tempfile

NUMERIC = re.compile(r'recomp_([0-9]+)\.c\Z')
PREAMBLE = re.compile(rb'/\*\*\r?\n(?: \*(?:(?!\*/)[^\r\n])*\r?\n)+ \*/\r?\n(?:\r?\n)*#define RECOMP_GENERATED_CODE\r?\n#include "recomp_funcs.h"\r?\n#include <math.h>\r?\n(?:\r?\n)*')
HEADER = re.compile(rb'/\*\*\r?\n \* (sub_[0-9A-Fa-f]{8}|xbe_entry_point)\r?\n(?: \*(?:(?!\*/)[^\r\n])*\r?\n)* \*/\r?\nvoid ([A-Za-z_][A-Za-z0-9_]*)\(void\)\r?\n\{')


def sha(data):
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class Function:
    address: str
    symbol: str
    section: bytes


def closing_brace(data, start):
    """Find a complete C body, respecting comments, escapes and literals."""
    depth, pos = 1, start + 1
    macros = set()
    while pos < len(data):
        char = data[pos]
        if data[pos:pos+2] == b'/*':
            end = data.find(b'*/', pos+2)
            if end < 0:
                raise ValueError('unterminated generated comment')
            pos = end + 2
        elif data[pos:pos+2] == b'//':
            end = data.find(b'\n', pos+2)
            if end < 0:
                raise ValueError('unterminated generated body')
            # A backslash-newline would extend a C line comment; reject this
            # unexpected form instead of treating following braces as code.
            if data[pos:end].rstrip(b'\r').endswith(b'\\'):
                raise ValueError('unsupported continued line comment')
            pos = end + 1
        elif char in (34, 39):
            quote = char
            pos += 1
            while pos < len(data) and data[pos] != quote:
                if data[pos] == 92:
                    pos += 2
                elif data[pos] in (10, 13):
                    raise ValueError('newline inside generated literal')
                else:
                    pos += 1
            if pos >= len(data):
                raise ValueError('unterminated generated literal')
            pos += 1
        elif char == 35:
            line_start = data.rfind(b'\n', 0, pos)+1
            if data[line_start:pos].strip():
                raise ValueError('unexpected preprocessor marker')
            end = data.find(b'\n', pos)
            if end < 0:
                raise ValueError('unterminated generated directive')
            first = data[pos:end].rstrip(b'\r')
            directive = re.fullmatch(rb'#(define|undef) (fp_push|fp_pop|fp_top|fp_st1|fp_st)(?![A-Za-z0-9_])(.*)', first)
            if not directive:
                raise ValueError('unknown generated directive')
            kind, name, tail = directive.groups()
            if kind == b'define':
                if name in macros or not tail.startswith(b'('):
                    raise ValueError('invalid function-local macro')
                macros.add(name)
            else:
                if name not in macros or tail.strip():
                    raise ValueError('unpaired function-local macro')
                macros.remove(name)
            while data[pos:end].rstrip(b'\r').endswith(b'\\'):
                pos = end+1
                end = data.find(b'\n', pos)
                if end < 0:
                    raise ValueError('unterminated generated macro continuation')
            directive_bytes = data[line_start:end]
            if any(token in directive_bytes for token in (b'/*',b'//',b'"',b"'")):
                raise ValueError('unknown generated macro comment/literal')
            pos = end+1
        elif char == 92:
            raise ValueError('unexpected generated code line splice')
        else:
            if char == 123:
                depth += 1
            elif char == 125:
                depth -= 1
                if not depth:
                    if macros:
                        raise ValueError('function-local macro escapes its section')
                    return pos + 1
            pos += 1
    raise ValueError('unterminated generated function')


def parse_unit(data):
    if b'\0' in data:
        raise ValueError('NUL in generated unit')
    prefix = PREAMBLE.match(data)
    if not prefix:
        raise ValueError('unknown generated preamble')
    pos = prefix.end()
    functions = []
    while pos < len(data):
        header = HEADER.match(data, pos)
        if not header:
            raise ValueError(f'unknown generated top-level section at byte {pos}')
        annotation = header[1].decode('ascii')
        symbol = header[2].decode('ascii')
        if symbol != annotation and not symbol.startswith('pilot_'):
            raise ValueError('unknown generated function symbol')
        original = re.search(rb'^ \* Original: 0x([0-9A-Fa-f]{8}) - 0x[0-9A-Fa-f]{8} \([0-9]+ bytes, [0-9]+ insns\)\r?$', data[pos:header.end()], re.M)
        if not original or (annotation.startswith('sub_') and original[1].decode('ascii').upper() != annotation[4:].upper()):
            raise ValueError('inconsistent generated address annotation')
        address = original[1].decode('ascii').upper()
        end = closing_brace(data, header.end()-1)
        while end < len(data) and data[end] in b' \t\r\n':
            end += 1
        functions.append(Function(address, symbol, data[pos:end]))
        pos = end
    if not functions:
        raise ValueError('generated unit has no complete functions')
    return data[:prefix.end()], functions


def partition(directory, max_functions=64, max_bytes=512*1024):
    """Return complete planned bytes and a lossless stage receipt; no writes."""
    if type(max_functions) is not int or type(max_bytes) is not int or max_functions < 1 or max_bytes < 1:
        raise ValueError('unit budgets must be positive')
    directory = Path(directory)
    inputs = []
    ids = set()
    symbols = set()
    addresses = set()
    for path in directory.iterdir():
        match = NUMERIC.fullmatch(path.name)
        if not match:
            continue
        index = int(match[1])
        if path.is_symlink() or not path.is_file() or index in ids:
            raise ValueError('ambiguous numeric generated unit')
        ids.add(index)
        data = path.read_bytes()
        try:
            prefix, functions = parse_unit(data)
        except ValueError as error:
            raise ValueError(f'{path.name}: {error}') from error
        for function in functions:
            if function.symbol in symbols or function.address in addresses:
                raise ValueError('duplicate generated function identity')
            symbols.add(function.symbol)
            addresses.add(function.address)
        inputs.append((index, path.name, data, prefix, functions))
    if not inputs:
        raise ValueError('no numeric generated units')
    next_index = max(ids)+1
    outputs = {}
    receipt = dict(schema='xboxllr.native-unit-budget.v1', max_functions=max_functions,
                   max_bytes=max_bytes, inputs=[], outputs=[], changed=False)
    for _, name, original, prefix, functions in sorted(inputs):
        groups, group, size = [], [], len(prefix)
        for function in functions:
            if group and (len(group) >= max_functions or size+len(function.section) > max_bytes):
                groups.append(group)
                group, size = [], len(prefix)
            group.append(function)
            size += len(function.section)
        groups.append(group)
        names = [name]
        for _ in groups[1:]:
            allocated=f'recomp_{next_index:04d}.c'
            if (directory/allocated).exists():
                raise ValueError('new numeric filename collides with an existing path')
            names.append(allocated)
            next_index += 1
        reconstructed = []
        for output_name, group in zip(names, groups):
            data = prefix+b''.join(function.section for function in group)
            # Validate the actual output parser, not just the slicing indices.
            parsed_prefix, parsed_functions = parse_unit(data)
            if parsed_prefix != prefix or parsed_functions != group:
                raise ValueError('partition changed generated function bytes')
            reconstructed.extend(parsed_functions)
            outputs[output_name] = data
            receipt['outputs'].append(dict(name=output_name, sha256=sha(data), bytes=len(data),
                functions=len(group), original=name,
                indivisible=[f.symbol for f in group] if len(data)>max_bytes else []))
        if prefix+b''.join(f.section for f in reconstructed) != original:
            raise ValueError('partition is not lossless')
        receipt['inputs'].append(dict(name=name, sha256=sha(original), bytes=len(original),
            functions=len(functions), outputs=names,
            sections=[dict(address=f.address, symbol=f.symbol, sha256=sha(f.section)) for f in functions]))
        receipt['changed'] |= len(groups)>1
    return outputs, receipt


def apply_budget(directory, max_functions=64, max_bytes=512*1024):
    """Validate every unit before writing; preserve unchanged files and mtimes."""
    directory = Path(directory)
    outputs, receipt = partition(directory, max_functions, max_bytes)
    for original in receipt['inputs']:
        if sha((directory/original['name']).read_bytes()) != original['sha256']:
            raise ValueError('generated input changed during partition planning')
    # Stage every changed file before replacement. A failed format preflight
    # performs no mutation; interrupted publication remains detectable by hashes.
    staged = []
    try:
        for name, data in outputs.items():
            destination = directory/name
            if destination.exists() and destination.read_bytes() == data:
                continue
            fd, temporary = tempfile.mkstemp(prefix='.native-unit-', dir=directory)
            staged.append((Path(temporary), destination))
            with os.fdopen(fd, 'wb') as stream:
                stream.write(data)
        for temporary, destination in staged:
            os.replace(temporary, destination)
    finally:
        for temporary, _ in staged:
            if temporary.exists():
                temporary.unlink()
    return receipt


def apply(directory, *, max_functions=64, max_bytes=524288):
    return apply_budget(directory, max_functions, max_bytes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--max-functions', type=int, default=64)
    parser.add_argument('--max-bytes', type=int, default=512*1024)
    parser.add_argument('--apply', action='store_true', help='publish the validated partition')
    parser.add_argument('--receipt', type=Path, help='write the stage receipt outside generated code')
    args = parser.parse_args()
    if args.receipt and args.receipt.resolve().is_relative_to(args.directory.resolve()):
        parser.error('receipt must stay outside the generated directory')
    receipt = (apply_budget(args.directory,args.max_functions,args.max_bytes) if args.apply else
               partition(args.directory,args.max_functions,args.max_bytes)[1])
    encoded = json.dumps(receipt,indent=2)+'\n'
    if args.receipt:
        args.receipt.write_text(encoded,encoding='utf-8')
    else:
        print(encoded,end='')


if __name__ == '__main__':
    main()
