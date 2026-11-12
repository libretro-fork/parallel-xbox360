"""Untimed typed D3D12 parent/bank images; no throughput or cold-play acceptance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('utilities', type=Path)
    parser.add_argument('reference', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--hardware', action='store_true')
    parser.add_argument('--debug', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    for key in tuple(env):
        if key.startswith(('IDENTITY_', 'MSAA_')):
            env.pop(key)
    env['IDENTITY_LEGACY_PREFIX'] = str(args.reference.resolve())
    if args.hardware:
        env['MSAA_HARDWARE'] = '1'
    if args.debug:
        env['IDENTITY_MSAA_D3D12_DEBUG'] = '1'
    files = [args.executable.resolve()]
    for prefix in (args.utilities, args.reference):
        files.extend(sorted(prefix.parent.glob(prefix.name + '-*.dxil')))
    manifest = dict(inputs=[dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for path in files],
                    hardware=args.hardware, sdk_validation=args.debug, timed=False)
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    with (args.output / 'execution.log').open('x', encoding='utf-8') as log:
        result = subprocess.run([str(args.executable.resolve()), str(args.utilities.resolve())],
                                env=env, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    data = (args.output / 'execution.log').read_text(encoding='utf-8')
    if result.returncode or re.search(r'API validation error|VUID-', data, re.IGNORECASE):
        raise RuntimeError('Typed D3D12 comparison or API validation failed; evidence retained')
    if 'IDENTITY_MSAA_CONTROL sample0 scale0 mask0' not in data:
        raise RuntimeError('Contaminated comparison flags')
    meta = re.search(r'IDENTITY_MSAA_D3D12 hardware([01]) native2=([01]) typedResources=1', data)
    images = re.search(r'PASS: (?:WARP|hardware D3D12) (\d+) numeric parent/bank MSAA images', data)
    if not meta or not images:
        raise RuntimeError('Missing typed resource or image metadata')
    hardware, native = map(int, meta.groups())
    if hardware != args.hardware or int(images[1]) != (native + 1) * 216:
        raise RuntimeError('Wrong adapter class or incomplete parent/bank corpus')
    if args.debug and 'IDENTITY_D3D12_API_VALIDATION enabled' not in data:
        raise RuntimeError('SDK validation was not enabled')
    report = dict(images=int(images[1]), native2=native, hardware=hardware,
                  sdk_validation=args.debug, correctness_verified=True, throughput_verified=False)
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
