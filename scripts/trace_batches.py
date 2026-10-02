#!/usr/bin/env python3
"""Decode versioned, checksummed deferred printf batches into legacy trace text."""
import argparse
import base64
import json
from pathlib import Path
import re
import struct
import time
import zlib

MAX_BYTES = 64 * 1024 * 1024
SPEC = re.compile(r'%(?:\.(\d+))?(?:ll|l|z)?([dugs%])')


def format_record(template, arguments):
    result, cursor, index = [], 0, 0
    while cursor < len(template):
        percent = template.find('%', cursor)
        if percent < 0:
            result.append(template[cursor:]); break
        result.append(template[cursor:percent])
        match = SPEC.match(template, percent)
        if not match:
            raise ValueError('Unsupported deferred format')
        precision, kind = match.groups()
        if kind == '%':
            result.append('%')
        else:
            if index == len(arguments):
                raise ValueError('Missing deferred argument')
            tag, value = arguments[index]; index += 1
            if kind in 'du' and tag in 'iu':
                result.append(str(value))
            elif kind == 's' and tag == 's':
                result.append(value)
            elif kind == 'g' and tag == 'd' and int(precision or 6) <= 17:
                result.append(format(value, '.' + (precision or '6') + 'g'))
            else:
                raise ValueError('Deferred argument type/precision mismatch')
        cursor = match.end()
    if index != len(arguments):
        raise ValueError('Extra deferred arguments')
    return ''.join(result)


def decode_records(payload):
    offset, calls, dictionary, output = 0, 0, {}, []
    def take(count):
        nonlocal offset
        if offset + count > len(payload):
            raise ValueError('Truncated binary trace record')
        data = payload[offset:offset + count]; offset += count
        return data
    def number(width):
        return int.from_bytes(take(width), 'little')
    def string():
        count = number(2)
        if count > 4096:
            raise ValueError('Trace string exceeds bound')
        return take(count).decode('ascii')
    while offset < len(payload):
        op, key = number(1), number(2)
        if op == 1:
            if key != len(dictionary) or key >= 256:
                raise ValueError('Invalid/duplicate trace dictionary entry')
            dictionary[key] = string()
        elif op == 2:
            if key not in dictionary:
                raise ValueError('Undefined trace dictionary entry')
            arguments = []
            for _ in range(number(1)):
                tag = take(1).decode('ascii')
                if tag == 's': value = string()
                elif tag in ('i', 'u', 'd'):
                    value = struct.unpack({'i': '<q', 'u': '<Q', 'd': '<d'}[tag], take(8))[0]
                else: raise ValueError('Unknown binary argument type')
                arguments.append((tag, value))
            output.append(format_record(dictionary[key], arguments)); calls += 1
        else:
            raise ValueError('Unknown binary trace opcode')
    return ''.join(output), calls


def decode_console(text):
    """Legacy text passes through unchanged. Incomplete/corrupt batches fail closed."""
    if 'ILLIXR_BATCH_' not in text:
        return text, {'encoding': 'legacy'}
    chunks, output, begun, ended = [], [], False, False
    total = 0
    for line in text.splitlines(keepends=True):
        if line.startswith('ILLIXR_BATCH_BEGIN'):
            if begun or line.strip() != 'ILLIXR_BATCH_BEGIN 1':
                raise ValueError('Duplicate or unsupported batch begin')
            begun = True
        elif line.startswith('ILLIXR_BATCH_V1 '):
            if not begun or ended:
                raise ValueError('Trace batch outside export')
            parts = line.split()
            if len(parts) != 5:
                raise ValueError('Malformed trace batch envelope')
            _, seq, count, checksum, encoded = parts
            if int(seq) != len(chunks) or not 1 <= int(count) <= 16384:
                raise ValueError('Missing/duplicate/out-of-order batch or invalid size')
            data = base64.b64decode(encoded, validate=True)
            if len(data) != int(count) or zlib.crc32(data) != int(checksum, 16):
                raise ValueError('Trace batch length/checksum mismatch')
            total += len(data)
            if total > MAX_BYTES: raise ValueError('Trace export exceeds bound')
            chunks.append(data)
        elif line.startswith('ILLIXR_BATCH_END '):
            parts = line.split()
            if not begun or ended or len(parts) != 5:
                raise ValueError('Unexpected/malformed batch end')
            _, count, size, expected_calls, status = parts
            if status != 'ok' or int(count) != len(chunks) or int(size) != total:
                raise ValueError('Incomplete batch export')
            decoded, calls = decode_records(b''.join(chunks))
            if calls != int(expected_calls): raise ValueError('Trace record count mismatch')
            output.append(decoded); ended = True
        elif line.startswith('ILLIXR_BATCH_'):
            raise ValueError('Unknown batch protocol/version')
        else:
            # Blank terminal framing may occur between batches. Preserve other
            # driver diagnostics without interpreting them as trace records.
            if begun and not ended and line.startswith('ILLIXR_'):
                raise ValueError('Unexpected firmware record during batch export')
            output.append(line)
    if not begun or not ended:
        raise ValueError('Missing batch begin/end: trace is incomplete')
    decoded_console = ''.join(output)
    return decoded_console, {'encoding': 'deferred-printf-v1', 'batches': len(chunks),
                             'binary_bytes': total, 'format_calls': calls,
                             'wire_console_bytes': len(text.encode()),
                             'decoded_console_bytes': len(decoded_console.encode())}


def decode_file(path):
    path = Path(path)
    original = path.read_bytes()
    text = original.decode('utf-8')
    started = time.perf_counter()
    decoded, stats = decode_console(text)
    if stats['encoding'] != 'legacy':
        stats['host_decode_seconds'] = time.perf_counter() - started
        raw = path.with_name('console.batched.log')
        if raw.exists(): raise ValueError('Refusing to overwrite original batch log')
        raw.write_bytes(original); path.write_text(decoded)
        path.with_name('trace-transfer.json').write_text(json.dumps(stats, indent=2) + '\n')
    return stats


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console', type=Path)
    args = parser.parse_args()
    print(json.dumps(decode_file(args.console)))
