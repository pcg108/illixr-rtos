#!/usr/bin/env python3
"""Prepare a private pinned Zephyr checkout with SDK RV64 multilib selection."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

REVISION = 'cd45a528d3bf81f9c7aa2a63f9fe512eee0881b0'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    if args.destination.exists():
        parser.error('destination must be new; the existing checkout is preserved')
    revision = subprocess.check_output(
        ['git', '-C', str(args.source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != REVISION:
        parser.error('unexpected Zephyr revision; review the patch before upgrading')
    patch = Path(__file__).resolve().parents[1] / 'patches/zephyr-vector-sdk-multilib.patch'
    subprocess.run(['git', 'clone', '--shared', str(args.source), str(args.destination)], check=True)
    subprocess.run(['git', '-C', str(args.destination), 'checkout', '--detach', REVISION], check=True)
    subprocess.run(['git', '-C', str(args.destination), 'apply', str(patch)], check=True)
    manifest = {'revision': REVISION, 'patch_sha256': hashlib.sha256(patch.read_bytes()).hexdigest(),
                'purpose': 'scalar SDK RV64/lp64d runtime with explicit vector context assembly'}
    args.destination.with_suffix('.vector-sdk.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
