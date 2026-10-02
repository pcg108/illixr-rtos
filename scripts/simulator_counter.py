"""Describe simulator watchdog counters without conflating clock domains."""
import hashlib
import json

LIMIT = 100_000_000_000


def counter_parameters(platform, simulator, manifest=None):
    if platform == 'spike':
        if manifest is not None:
            raise ValueError('Spike uses instruction counts, not an RTL clock manifest')
        return dict(counter_limit=LIMIT, counter_semantics='Spike instruction limit')
    result = dict(counter_limit=LIMIT,
                  counter_semantics='TestDriver reference-clock increments; period not supplied')
    if manifest is None:
        return result
    data = json.loads(manifest.read_text())
    digest = hashlib.sha256(simulator.read_bytes()).hexdigest()
    if data.get('model_sha256') != digest:
        raise ValueError('RTL clock manifest does not match simulator executable')
    period, core_period = data.get('testdriver_period_ns'), data.get('rocket_period_ns')
    # These are the two separately built harness variants for the unchanged
    # 500 MHz target. Never guess a clock ratio for an unknown model.
    if period not in (1.0, 2.0) or core_period != 2.0:
        raise ValueError('Unsupported or missing RTL reference/Rocket clock periods')
    ratio = int(core_period / period)
    return dict(counter_limit=LIMIT*ratio,
                counter_semantics='TestDriver reference-clock increments',
                nominal_rocket_cycle_limit=LIMIT,
                testdriver_period_ns=period, rocket_period_ns=core_period,
                reference_increments_per_rocket_cycle=ratio,
                counter_manifest=str(manifest),
                counter_manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest())
