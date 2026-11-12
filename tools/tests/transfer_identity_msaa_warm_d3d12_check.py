"""Run typed D3D12 MSAA work/image checks; keep diagnostic and throughput acceptance separate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from transfer_identity_msaa_warm_analysis import analyze


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('utilities', type=Path)
    parser.add_argument('reference', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--reverse', action='store_true')
    parser.add_argument('--hardware', action='store_true')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--diagnostic', action='store_true')
    parser.add_argument('--control', choices=('stale', 'sample', 'extra'))
    args = parser.parse_args()
    if args.debug and not (args.diagnostic or args.verify_only or args.control in ('stale', 'sample')):
        parser.error('SDK-instrumented runs cannot qualify hardware throughput')
    if args.verify_only and args.control:
        parser.error('Image-only mode cannot exercise timed corruption controls')
    args.output.mkdir(parents=True, exist_ok=False)
    clean = os.environ.copy()
    for flag in tuple(clean):
        if flag.startswith(('IDENTITY_', 'MSAA_', 'XE_SHADER_BENCH_')):
            clean.pop(flag)
    flags = {'IDENTITY_REVERSE_ORDER': args.reverse,
             'MSAA_HARDWARE': args.hardware,
             'IDENTITY_MSAA_D3D12_DEBUG': args.debug,
             'IDENTITY_MSAA_VERIFY_ONLY': args.verify_only,
             'IDENTITY_MSAA_STALE_PROBE': args.control == 'stale',
             'IDENTITY_MSAA_BAD_TIMED_SAMPLE': args.control == 'sample',
             'IDENTITY_EXTRA_WORK': args.control == 'extra'}
    clean.update({name: '1' for name, enabled in flags.items() if enabled})
    clean['IDENTITY_LEGACY_PREFIX'] = str(args.reference.resolve())
    clean['IDENTITY_GPU_CSV'] = str((args.output / 'gpu.csv').resolve())
    clean['XE_SHADER_BENCH_CSV'] = str((args.output / 'initialization.csv').resolve())
    inputs = [args.executable.resolve()]
    for prefix in (args.utilities, args.reference):
        inputs.extend(sorted(prefix.parent.glob(prefix.name + '-*.dxil')))
    manifest = dict(inputs=[dict(path=str(path), sha256=digest(path)) for path in inputs],
                    flags=flags, diagnostic=args.diagnostic,
                    driver_cache='uncontrolled driver disk cache; warmed in-process objects only',
                    sdk_validation=args.debug, hardware=args.hardware,
                    command=[str(args.executable.resolve()), str(args.utilities.resolve())])
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    with (args.output / 'execution.log').open('x', encoding='utf-8') as log:
        result = subprocess.run(manifest['command'], env=clean, stdout=log,
                                stderr=subprocess.STDOUT, timeout=3600)
    data = (args.output / 'execution.log').read_text(encoding='utf-8')
    if re.search(r'VUID-|validation error', data, re.IGNORECASE):
        raise RuntimeError('D3D12 validation failed; evidence retained')
    meta = re.search(r'IDENTITY_MSAA_D3D12 hardware([01]) native2=([01]) typedResources=1', data)
    if not meta:
        raise RuntimeError('Missing device/image proof')
    hardware, native = map(int, meta.groups())
    if hardware != args.hardware:
        raise RuntimeError('Incorrect adapter class')
    tx, ty = (1, 1) if args.diagnostic else (16, 8)
    if f'IDENTITY_MSAA_WORKLOAD tiles{tx},{ty} verifyOnly{int(args.verify_only)}' not in data:
        raise RuntimeError('Wrong image size or verification mode')
    if args.debug and 'IDENTITY_D3D12_API_VALIDATION enabled' not in data:
        raise RuntimeError('SDK validation not enabled')
    if args.control in ('stale', 'sample'):
        if not result.returncode or 'MSAA timed-batch image mismatch' not in data:
            raise RuntimeError('Image corruption control did not fail at the image oracle')
        report = dict(accepted=False, control_rejected=True, control=args.control)
    else:
        if result.returncode:
            raise RuntimeError(f'Work/image test failed: exit {result.returncode}')
        images = re.search(r'PASS: (?:WARP|hardware D3D12) (\d+) numeric parent/bank MSAA images', data)
        if not images:
            raise RuntimeError('Missing image proof')
        if int(images[1]) != (native + 1) * 216:
            raise RuntimeError('Incomplete image corpus')
        if args.verify_only:
            report = dict(accepted=False, correctness_verified=True, throughput_verified=False)
        else:
            report = analyze(args.output / 'gpu.csv', args.reverse,
                             args.control == 'extra', args.diagnostic, backend='d3d12')
        report['image_count'] = int(images[1])
        if args.control == 'extra':
            report['control_rejected'] = len(report['failures']) == len(report['cases'])
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({key: report[key] for key in ('accepted', 'image_count', 'control_rejected') if key in report}))
    if args.control:
        return 0 if report.get('control_rejected') else 1
    return 0 if args.verify_only or args.diagnostic or report['accepted'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
