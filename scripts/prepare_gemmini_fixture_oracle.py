#!/usr/bin/env python3
"""Prepare standalone-only oracle copies; leave production source files intact.

Capture uses the original target calculations under Spike. Verify compares every
table value bit-for-bit with those calculations. Table mode elides only oracle
calculations, retaining all BLAS invocations and result checks.
"""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Standalone oracle source hook changed: ' + old[:90])
    return text.replace(old, new)


def capture_tables(path):
    products, triangles, values, end = [], [], [], []
    remaining = 0
    for line in path.read_text().splitlines():
        if line.startswith('ILLIXR_FIXTURE_G '):
            a, b, k, exact = line.split()[1:]
            products.append((int(a, 16), int(b, 16), int(k), int(exact)))
        elif line.startswith('ILLIXR_FIXTURE_T '):
            if remaining:
                raise ValueError('Incomplete triangular reference')
            key, count = line.split()[1:]
            remaining = int(count)
            triangles.append((int(key, 16), len(values), remaining))
        elif line.startswith('ILLIXR_FIXTURE_V '):
            if not remaining:
                raise ValueError('Unexpected reference value')
            values.append(int(line.split()[1], 16)); remaining -= 1
        elif line.startswith('ILLIXR_FIXTURE_ORACLE '):
            end.append(json.loads(line.split(' ', 1)[1]))
    if (remaining or len(end) != 1 or end[0] != {'mode': 'capture', 'passed': True,
            'products': len(products), 'triangles': len(triangles), 'errors': 0}):
        raise ValueError('Capture is incomplete or did not pass')
    if len(products) != 253728 or len(triangles) != 80 or len(values) != 151296:
        raise ValueError('Capture does not cover the full fixture')
    if 'ILLIXR_GEMMINI_STANDALONE_END pass' not in path.read_text():
        raise ValueError('Capture lacks successful complete standalone termination')
    return products, triangles, values


def prepare(root, output, mode, capture=None):
    output.mkdir(parents=True, exist_ok=True)
    source = root / 'src/gemmini_selftest.cpp'
    text = '#include "fixture_oracle.hpp"\n' + source.read_text()
    text = replace_once(text, ' ++checks;\n',
                        ' ++checks;\n if(!fixture_oracle::product(expected,scale,k,exact)) return false;\n')
    if mode == 'table':
        start = text.index('    if(i<m) {\n')
        finish = text.index('    good=check(c[i+j*ldc]', start)
        text = text[:start] + text[finish:]
        text = replace_once(text,
            "    for(int j=0;j<nx;++j) { double p=double(a[t=='N'?i+j*lda:j+i*lda])*double(x[x0+j*ix]);sum+=p;absolute+=std::abs(p); }\n", '')
    (output / source.name).write_text(text)
    reference = root / 'src/blas_reference.cpp'
    text = '#include "fixture_oracle.hpp"\n' + reference.read_text()
    hook = "  const int order=side=='L'?m:n;\n"
    text = replace_once(text, hook,
        '  const auto fixture_key=fixture_oracle::triangle_key(side,uplo,transpose,diagonal,m,n,alpha,a,lda,b,ldb);\n' + hook)
    if mode == 'table':
        start = text.index('  Matrix mat=matrix(a,order,order,lda),input=matrix(b,m,n,ldb);')
        finish = text.index('\n}\n}', start)
        text = text[:start] + '  fixture_oracle::triangle(fixture_key,b,ldb*n);' + text[finish:]
    else:
        text = replace_once(text,
            '  for(int j=0;j<n;++j) for(int i=0;i<m;++i) b[i+j*ldb]=result(i,j);',
            '  for(int j=0;j<n;++j) for(int i=0;i<m;++i) b[i+j*ldb]=result(i,j);\n  fixture_oracle::triangle(fixture_key,b,ldb*n);')
    (output / reference.name).write_text(text)
    main = root / 'tests/gemmini/main.cpp'
    text = '#include "fixture_oracle.hpp"\n' + main.read_text()
    text = replace_once(text, ' gemmini_backend::shutdown();',
                        ' good=fixture_oracle::finish()&&good;\n gemmini_backend::shutdown();')
    (output / 'main.cpp').write_text(text)
    manifest = {'mode': mode, 'production_sources_unchanged': True,
                'sources': {str(p.relative_to(root)): sha(p) for p in
                            (source, reference, main, root / 'tests/gemmini/fixture_oracle.hpp')},
                'transformed': {p.name: sha(p) for p in output.glob('*.cpp')}}
    if mode != 'capture':
        if capture is None:
            raise ValueError('Verify/table mode requires a successful capture')
        provenance = capture.parent / 'oracle_manifest.json'
        captured = json.loads(provenance.read_text())
        if captured.get('mode') != 'capture' or captured.get('sources') != manifest['sources']:
            raise ValueError('Captured oracle source provenance differs from this build')
        products, triangles, values = capture_tables(capture)
        data = ['#include "fixture_oracle.hpp"', 'namespace fixture_oracle {',
                'const Product products[] = {']
        data += [f'{{0x{a:016x}ull,0x{b:016x}ull,{k},{bool(exact).__str__().lower()}}},' for a,b,k,exact in products]
        data += ['};', 'const Triangle triangles[] = {']
        data += [f'{{0x{key:016x}ull,{offset},{count}}},' for key,offset,count in triangles]
        data += ['};', 'const uint64_t triangle_values[] = {']
        data += [f'0x{value:016x}ull,' for value in values]
        data += ['};', f'const size_t product_count={len(products)}, triangle_count={len(triangles)};', '}']
        path = output / 'fixture_oracle_data.cpp';path.write_text('\n'.join(data)+'\n')
        manifest.update(capture=str(capture.resolve()), capture_sha256=sha(capture),
                        capture_manifest_sha256=sha(provenance),
                        product_count=len(products), triangle_count=len(triangles),
                        triangle_value_count=len(values), table_source_sha256=sha(path))
    (output / 'oracle_manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('capture','verify','table'), required=True)
    parser.add_argument('--capture', type=Path)
    args = parser.parse_args()
    prepare(args.root, args.output, args.mode, args.capture)
