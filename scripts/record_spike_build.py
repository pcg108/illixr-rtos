#!/usr/bin/env python3
"""Save immutable artifact and source fingerprints for a Spike build."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

repo, deps, artifact = map(Path, sys.argv[1:])
def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args])
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

paths = git(repo, 'ls-files', '-z', '--cached', '--others', '--exclude-standard').decode().split('\0')
state = {
    'toolchain': {
        'sdk_version': (deps / 'zephyr-sdk-0.17.0/sdk_version').read_text().strip(),
        'compiler_version': subprocess.check_output([
            str(deps / 'zephyr-sdk-0.17.0/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-g++'),
            '--version']).decode().splitlines()[0],
        'runtime_package_sha256': 'cd97784c88de0207c93cf386f79d8d2606b46598dc74d4ad12cadd5617595964',
    },
    'application_commit': git(repo, 'rev-parse', 'HEAD').decode().strip(),
    'source_sha256': {p: sha(repo / p) for p in paths if p and (repo / p).is_file()},
    'dependency_commits': {p: git(deps / p, 'rev-parse', 'HEAD').decode().strip()
                           for p in ('zephyr', 'opencv', 'modules/lib/eigen')},
    'artifact_sha256': {p.name: sha(p) for p in artifact.iterdir() if p.is_file()
                       and p.name != 'build_manifest.json'},
}
(artifact / 'build_manifest.json').write_text(json.dumps(state, indent=2) + '\n')
