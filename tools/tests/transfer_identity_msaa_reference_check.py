"""Untimed parent/production-bank comparison on normal typed Vulkan images."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('utilities', type=Path)
    parser.add_argument('reference', type=Path)
    parser.add_argument('logs', type=Path)
    args = parser.parse_args()
    args.logs.mkdir(parents=True, exist_ok=True)
    clean = os.environ.copy()
    for flag in ('MSAA_DYNAMIC_RENDERING', 'MSAA_DISABLE_SAMPLE_SHADING',
                 'IDENTITY_MSAA_BAD_SAMPLE', 'IDENTITY_MSAA_BAD_SCALE',
                 'IDENTITY_MSAA_BAD_MASK', 'IDENTITY_MSAA_TYPED_RESOURCES'):
        clean.pop(flag, None)
    clean['IDENTITY_LEGACY_PREFIX'] = str(args.reference)
    for name, settings in (
        ('render-pass', {}),
        ('manual', {'MSAA_DISABLE_SAMPLE_SHADING': '1'}),
        ('dynamic', {'MSAA_DYNAMIC_RENDERING': '1'}),
        ('dynamic-manual', {'MSAA_DYNAMIC_RENDERING': '1', 'MSAA_DISABLE_SAMPLE_SHADING': '1'}),
    ):
        env = clean.copy()
        env.update(settings)
        path = args.logs / (name + '.log')
        with path.open('x', encoding='utf-8') as output:
            result = subprocess.run([str(args.executable.resolve()), str(args.utilities)],
                                    env=env, stdout=output, stderr=subprocess.STDOUT, timeout=180)
        data = path.read_text(encoding='utf-8')
        if result.returncode or re.search(r'validation error|VUID-', data, re.IGNORECASE):
            raise RuntimeError(f'Numeric MSAA reference/API verification failed: {path}')
        if 'IDENTITY_MSAA_CONTROL sample0 scale0 mask0' not in data:
            raise RuntimeError(f'Contaminated reference flags: {path}')
        meta = re.search(r'native2=([01]) sampleShading=([01]) dynamicRendering=([01]) typedResources=1', data)
        count = re.search(r'PASS: (\d+) typed numeric MSAA parent/bank images', data)
        if not meta or not count:
            raise RuntimeError(f'Missing reference/image metadata: {path}')
        native, shading, dynamic = map(int, meta.groups())
        if dynamic != int('MSAA_DYNAMIC_RENDERING' in settings):
            raise RuntimeError(f'Wrong reference rendering mode: {path}')
        if 'MSAA_DISABLE_SAMPLE_SHADING' in settings and shading:
            raise RuntimeError(f'Manual reference not exercised: {path}')
        if int(count[1]) != (native + 1) * 4 * 3 * (5 if shading else 3) * 2:
            raise RuntimeError(f'Incomplete parent/bank image corpus: {path}')
        print(f'Verified typed parent/bank {name}: {path}', flush=True)


if __name__ == '__main__':
    main()
