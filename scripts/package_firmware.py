#!/usr/bin/env python3
"""Package a completed Rocket/FireSim CMake build with verified provenance.

This command copies build outputs and runs the existing ABI, BLAS and platform
checks. It does not compile firmware or operate an FPGA. The destination is
published only after every check succeeds.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from record_rocket_build import sha, validate_platform
from firmware_profile import profile_metadata, verified_pipeline


TRUE_VALUES = {'1', 'ON', 'TRUE', 'YES', 'Y'}


def settings(path):
    result = {}
    for line in path.read_text().splitlines():
        if '=' in line and not line.startswith(('#', '//')):
            key, value = line.split('=', 1)
            result[key.split(':', 1)[0]] = value
    return result


def derive_target(cache, config, platform):
    harts = int(config['CONFIG_MP_MAX_NUM_CPUS'])
    if harts not in (1, 2, 4):
        raise ValueError('Rocket packaging supports one, two, or four harts')
    timer = int(config['CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC'])
    core = int(cache['ILLIXR_CORE_HZ'])
    scale = timer // platform['timer_hz']
    if scale not in (1, 2) or timer != platform['timer_hz'] * scale or core != platform['core_hz'] * scale:
        raise ValueError('Compiled CPU/timer declarations must preserve the generated ratio at scale 1 or 2')
    return {'harts': harts,
            'placement': 'pinned' if cache['ILLIXR_PIN_PLUGINS'].upper() in TRUE_VALUES else 'scheduler',
            'preflight': int(cache['ILLIXR_PLATFORM_CHECK_ONLY'].upper() in TRUE_VALUES),
            'modeled_clock_scale': scale}


def package(args):
    build, deps, platform_path, output, repo = (getattr(args, name).resolve()
        for name in ('build', 'deps', 'platform', 'output', 'repo'))
    if output.exists():
        raise ValueError(f'Refusing to overwrite existing package: {output}')
    cache = settings(build / 'CMakeCache.txt')
    config = settings(build / 'zephyr/.config')
    platform = json.loads(platform_path.read_text())
    target = derive_target(cache, config, platform)
    validate_platform(platform_path, target['harts'])
    expected_paths = {'ZEPHYR_BASE': deps / 'zephyr', 'OPENCV_SRC_DIR': deps / 'opencv',
                      'ZEPHYR_SDK_INSTALL_DIR': deps / 'zephyr-sdk-0.17.0',
                      'APPLICATION_SOURCE_DIR': repo}
    for key, expected in expected_paths.items():
        if key in cache and Path(cache[key]).resolve() != expected.resolve():
            raise ValueError(f'{key} in the build differs from the selected repository/dependencies')
    generated = (build / cache['ILLIXR_DATA_INCLUDE_DIR']).resolve()
    inputs = {'zephyr.elf': build / 'zephyr/zephyr.elf', '.config': build / 'zephyr/.config',
              'zephyr.dts': build / 'zephyr/zephyr.dts', 'CMakeCache.txt': build / 'CMakeCache.txt',
              'dataset_manifest.json': generated / 'dataset_manifest.json'}
    for source in inputs.values():
        if not source.is_file():
            raise ValueError(f'Required completed-build output is missing: {source}')
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f'.{output.name}-packaging-', dir=output.parent))
    commands = []
    try:
        for name, source in inputs.items():
            shutil.copy2(source, staging / name)
        generated_identity = {}
        for name in ('embedded_imu.hpp', 'embedded_cam.hpp', 'embedded_dataset.hpp'):
            source = generated / name
            generated_identity[name] = {'path': str(source), 'bytes': source.stat().st_size,
                                        'sha256': sha(source)}
        (staging / 'generated-inputs.json').write_text(json.dumps(generated_identity, indent=2) + '\n')
        if not cache.get('YAML_FILE'):
            raise ValueError('Build cache lacks the selected YAML_FILE profile')
        shutil.copy2((repo / cache['YAML_FILE']).resolve(), staging / 'profile.yaml')
        pipeline = profile_metadata(staging / 'profile.yaml')
        prefix = deps / 'zephyr-sdk-0.17.0/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-'

        def run(command, log):
            commands.append([str(value) for value in command])
            with (staging / log).open('w') as stream:
                subprocess.run(commands[-1], stdout=stream, stderr=subprocess.STDOUT, check=True)

        run([str(prefix) + 'size', staging / 'zephyr.elf'], 'size.txt')
        run([str(prefix) + 'readelf', '-h', '-A', staging / 'zephyr.elf'], 'elf-attributes.txt')
        run([sys.executable, repo / 'scripts/audit_blas.py', '--build', build,
             '--artifact', staging, '--nm', str(prefix) + 'nm'], 'blas-audit.log')
        run([sys.executable, repo / 'scripts/record_rocket_build.py', '--repo', repo,
             '--deps', deps, '--artifact', staging, '--platform', platform_path,
             '--harts', str(target['harts']), '--placement', target['placement'],
             '--preflight', str(target['preflight']), '--modeled-clock-scale',
             str(target['modeled_clock_scale'])], 'record-build.log')
        # The recorder captured these copies; verify that the original build
        # did not change while audits were running before publishing the package.
        for name, source in inputs.items():
            if sha(source) != sha(staging / name):
                raise ValueError(f'Build output changed while packaging: {source}')
        provenance = {'complete': True, 'build': str(build), 'deps': str(deps),
                      'platform': str(platform_path), 'repo': str(repo),
                      'derived_target': target, 'commands': commands,
                      'source_scope': 'Repository/dependency fingerprints are collected at packaging time.'}
        (staging / 'package.json').write_text(json.dumps(provenance, indent=2) + '\n')
        manifest_path = staging / 'build_manifest.json'
        manifest = json.loads(manifest_path.read_text())
        manifest['pipeline'] = pipeline
        verified_pipeline(staging, manifest, required=True)
        manifest['artifact_sha256']['package.json'] = sha(staging / 'package.json')
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
        if output.exists():
            raise ValueError(f'Destination appeared during packaging: {output}')
        staging.rename(output)
    except BaseException:
        print(f'Packaging did not complete; diagnostic files remain in {staging}', file=sys.stderr)
        raise
    print(json.dumps({'complete': True, 'output': str(output), 'elf': str(output / 'zephyr.elf'),
                      'manifest': str(output / 'build_manifest.json'), **target}, indent=2))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'deps', 'platform', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    try:
        return package(args)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, f'{parser.prog}: {error}\n')


if __name__ == '__main__':
    sys.exit(main())
