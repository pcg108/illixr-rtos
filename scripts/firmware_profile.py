"""Read and verify the profile carried with a portable firmware package."""
import hashlib
from pathlib import Path


def profile_metadata(path):
    import yaml
    raw = Path(path).read_bytes()
    try:
        document = yaml.safe_load(raw)
    except yaml.YAMLError as error:
        raise ValueError(f'Invalid packaged plugin profile: {error}') from error
    if not isinstance(document, dict):
        raise ValueError('Packaged profile must be a YAML mapping')
    plugins = document.get('plugins')
    if isinstance(plugins, str):
        plugins = plugins.split(',')
    if (not isinstance(plugins, list) or not plugins or
            any(not isinstance(name, str) or not name.strip() for name in plugins)):
        raise ValueError('Packaged profile requires a nonempty plugin list')
    plugins = [name.strip() for name in plugins]
    if len(set(plugins)) != len(plugins):
        raise ValueError('Packaged profile contains duplicate plugins')
    selected = set(plugins)
    gpu = bool(selected & {'render_loop', 'timewarp'})
    eye = bool(selected & {'eye_tracking', 'offline_eye'})
    if gpu and not {'pose_prediction', 'render_loop', 'timewarp'} <= selected:
        raise ValueError('Packaged GPU profile requires prediction, render and timewarp plugins')
    if eye and (not gpu or not {'eye_tracking', 'offline_eye'} <= selected):
        raise ValueError('Packaged eye profile requires both eye plugins and the GPU pipeline')
    return {'profile_file': 'profile.yaml', 'profile_sha256': hashlib.sha256(raw).hexdigest(),
            'plugins': plugins, 'require_gpu': gpu, 'require_eye': eye}


def verified_pipeline(artifact, build, required=False):
    """Support legacy collection, but never trust unverified portable profiles."""
    path = Path(artifact) / 'profile.yaml'
    recorded = build.get('pipeline')
    expected_hash = build.get('artifact_sha256', {}).get('profile.yaml')
    if not path.is_file() and recorded is None and expected_hash is None and not required:
        return None
    if not path.is_file():
        raise ValueError('Portable firmware requires its preserved profile.yaml; repackage the completed build')
    profile = profile_metadata(path)
    if expected_hash != profile['profile_sha256']:
        raise ValueError('Packaged profile hash differs from the firmware manifest')
    if recorded is not None:
        if not isinstance(recorded, dict):
            raise ValueError('Firmware pipeline metadata must be an object')
        for flag in ('require_gpu', 'require_eye'):
            if type(recorded.get(flag)) is not bool:
                raise ValueError(f'Firmware pipeline {flag} must be a boolean')
        if any(recorded.get(key) != value for key, value in profile.items()):
            raise ValueError('Firmware pipeline flags/plugins differ from the preserved profile')
    if bool(build.get('ritnet', {}).get('enabled')) != profile['require_eye']:
        raise ValueError('Firmware eye metadata differs from the preserved profile')
    return profile
