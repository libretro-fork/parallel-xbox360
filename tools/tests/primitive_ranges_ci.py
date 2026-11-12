#!/usr/bin/env python3
"""Warm CPU range collection: exact parent, shared harness, fixed CPU, raw pairs.

No core/runtime dependency. No gameplay, RSS or whole-renderer acceptance.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

from primitive_ranges_analysis import compare, parse
from transfer_uint_warm_ci import ROOT, git, comparison_refs, execute, toolchain_identity
from transfer_uint_warm_d3d12_ci import build_record as native_build_record


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_record(path, revision, self_check=False):
    source = Path(path).resolve()
    if git(source, 'rev-parse', 'HEAD') != revision:
        raise ValueError('source revision mismatch')
    dirty = bool(git(source, 'status', '--porcelain'))
    if dirty and not self_check:
        raise ValueError('qualification requires clean sources')
    files = [source/'src/xenia/gpu'/name for name in ('primitive_processor.h', 'primitive_processor.cc')]
    fixed = source/'libretro/core_primitive_ranges.h'
    if fixed.exists():
        files.append(fixed)
    return {'source': str(source), 'revision': revision, 'dirty': dirty,
            'kind': 2 if fixed.exists() else 1,
            'files': {str(p.relative_to(source)): digest(p) for p in files}}


def build_record(path, source):
    result = native_build_record(path, source)
    build = Path(result['build'])
    if result['options']['CMAKE_BUILD_TYPE'] != 'Release' or result['options']['XENIA_ENABLE_LTO'] != 'OFF':
        raise ValueError('requires matched Release/LTO-OFF native builds')
    current = os.path.normcase(str(Path(shutil.which('cl') or '').resolve()))
    if result['options']['CMAKE_CXX_COMPILER'] != current:
        raise ValueError('harness compiler differs from native archive compiler')
    files = sorted((build/'obj/Windows').glob('*.lib'))
    files += [build/'libretro/CMakeFiles/xenia-libretro.dir'/name for name in ('core_events.c.obj', 'core_disc.c.obj')]
    if not files or not (build/'obj/Windows/xenia-gpu.lib').exists():
        raise ValueError('missing actual primitive implementation archive')
    result['link_inputs'] = {str(p.relative_to(build)): digest(p) for p in files}
    cache = (build/'CMakeCache.txt').read_text()
    ninja = re.search(r'^CMAKE_MAKE_PROGRAM:[^=]+=(.+)$', cache, re.M)
    if not ninja:
        raise ValueError('missing Ninja build identity')
    commands = subprocess.check_output([ninja[1], '-C', str(build), '-t', 'commands'], text=True)
    actual = [line for line in commands.splitlines()
              if re.search(r'[/\\]primitive_processor\.cc(?=["\s]|$)', line)]
    if len(actual) != 1:
        raise ValueError('missing/duplicate actual primitive compilation command')
    normalized = actual[0].replace('\\', '/')
    for directory, tag in ((build, '<BUILD>'), (Path(source).resolve(), '<SOURCE>')):
        normalized = re.sub(re.escape(directory.as_posix()), lambda _: tag, normalized, flags=re.I)
    result['compile_command'] = normalized
    return result


def harness_record():
    names = ('primitive_ranges_ci.py', 'primitive_ranges_analysis.py',
             'core_primitive_ranges_bench.c', 'primitive_counts_bridge.cc',
             'primitive_counts_protocol.h', 'build_primitive_ranges_bench.cmd',
             'shader_compile_bench.h', 'transfer_uint_warm_ci.py',
             'transfer_uint_warm_d3d12_ci.py', 'transfer_uint_warm_analysis.py')
    return {'revision': git(ROOT, 'rev-parse', 'HEAD'),
            'dirty': bool(git(ROOT, 'status', '--porcelain')),
            'files': {name: digest(ROOT/'tools/tests'/name) for name in names}}


def idle():
    script = "Get-Process -Name retroarch,cl,link,cc1plus,'g++' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty ProcessName | ConvertTo-Json -Compress; exit 0"
    text = subprocess.check_output(['powershell', '-NoProfile', '-Command', script], text=True).strip()
    if text and json.loads(text):
        raise ValueError('runner has active frontend/compiler/linker work: ' + text)


def matched_builds(builds):
    if builds[0]['options'] != builds[1]['options'] or builds[0]['compile_command'] != builds[1]['compile_command']:
        raise ValueError('native build options differ')


def finish_report(report, self_check, negative_control):
    report.update(accepted=report['forward']['accepted'] and report['reverse']['accepted'],
                  self_check=self_check, negative_control=negative_control)
    report['qualified'] = report['accepted'] and not self_check and not negative_control
    return report


def validate_negative(report):
    if any(0 not in report[order]['failures'] for order in ('forward', 'reverse')):
        raise ValueError('real-work negative case 0 must fail in both orders')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('base', 'head', 'parent-source', 'head-source', 'parent-build', 'head-build', 'out'):
        parser.add_argument('--'+name, required=True)
    parser.add_argument('--self-check', action='store_true')
    parser.add_argument('--negative-control', action='store_true')
    parser.add_argument('--cpu-group', type=int, default=0)
    parser.add_argument('--cpu-index', type=int, default=2)
    args = parser.parse_args()
    if os.name != 'nt':
        raise ValueError('current qualification runner requires MSVC/Windows')
    if not 0 <= args.cpu_group < 65536 or not 0 <= args.cpu_index < 64:
        raise ValueError('invalid CPU affinity')
    output = Path(args.out).resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError('fresh output required; never overwrite failed/noisy evidence')
    output.mkdir(parents=True, exist_ok=True)
    first, selected, head = comparison_refs(args.base, args.head, self_check=args.self_check)
    sources = [source_record(args.parent_source, selected, args.self_check),
               source_record(args.head_source, head, args.self_check)]
    harness = harness_record()
    if harness['dirty'] and not args.self_check:
        raise ValueError('commit common harness before qualification')
    env = dict(os.environ)
    for name in ('PRIMITIVE_BENCH_REVERSE', 'PRIMITIVE_BENCH_NEGATIVE'):
        env.pop(name, None)
    env.update(PRIMITIVE_BENCH_CPU_GROUP=str(args.cpu_group), PRIMITIVE_BENCH_CPU_INDEX=str(args.cpu_index))
    manifest = {'first_parent': first, 'sources': sources, 'harness': harness,
                'toolchain': toolchain_identity(), 'self_check': args.self_check,
                'negative_control': args.negative_control,
                'affinity': {'group': args.cpu_group, 'cpu': args.cpu_index},
                'warmup': 'trial0 retained/excluded; 16 unmeasured calls per case',
                'timing': 'wall ns/call gate; process CPU CSV supplementary',
                'coverage': 'warm private range collection, not gameplay/FPS/RSS acceptance'}
    try:
        builds = [build_record(args.parent_build, sources[0]['source']),
                  build_record(args.head_build, sources[1]['source'])]
        matched_builds(builds)
        for n, build in enumerate(builds):
            execute(['cmake', '--build', build['build'], '--target', 'xenia-libretro', '--parallel', '3'], output, env, 'native-build-%d.log' % n)
        builds = [build_record(args.parent_build, sources[0]['source']), build_record(args.head_build, sources[1]['source'])]
        matched_builds(builds)
        manifest['builds'] = builds
        for n, (source, build) in enumerate(zip(sources, builds)):
            destination = output/('parent' if n == 0 else 'candidate')
            execute([env.get('COMSPEC', 'cmd.exe'), '/d', '/c', str(ROOT/'tools/tests/build_primitive_ranges_bench.cmd'),
                     source['source'], build['build'], str(destination)], output, env, 'harness-build-%d.log' % n)
        binaries = {role: digest(output/role/'test.exe') for role in ('parent', 'candidate')}
        manifest['binaries'] = binaries
        (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        report = {}
        for reverse in (False, True):
            order_name = 'reverse' if reverse else 'forward'
            samples = [[], []]
            for trial in range(8):
                for n in ((0, 1) if (trial + reverse) % 2 == 0 else (1, 0)):
                    idle()
                    role = 'parent' if n == 0 else 'candidate'
                    stem = '%s-%d-%s' % (order_name, trial, role)
                    run_env = env.copy()
                    run_env.update(XE_SHADER_BENCH_CSV=str(output/(stem+'.csv')),
                                   XE_SHADER_BENCH_CPU_CSV=str(output/(stem+'-cpu.csv')))
                    if reverse:
                        run_env['PRIMITIVE_BENCH_REVERSE'] = '1'
                    if args.negative_control and n == 1:
                        run_env['PRIMITIVE_BENCH_NEGATIVE'] = '1'
                    execute([str(output/role/'test.exe')], output, run_env, stem+'.log', timeout=300)
                    log = (output/(stem+'.log')).read_text()
                    values = parse(log, (output/(stem+'.csv')).read_text(), sources[n]['kind'], reverse,
                                   args.cpu_group, args.cpu_index, args.negative_control and n == 1)
                    if trial:
                        samples[n].append(values)
            report[order_name] = compare(*samples)
        if harness != harness_record() or sources != [source_record(args.parent_source, selected, args.self_check), source_record(args.head_source, head, args.self_check)]:
            raise ValueError('sources/harness changed during measurement')
        if builds != [build_record(args.parent_build, sources[0]['source']), build_record(args.head_build, sources[1]['source'])]:
            raise ValueError('archive/options changed during measurement')
        if binaries != {role: digest(output/role/'test.exe') for role in ('parent', 'candidate')}:
            raise ValueError('benchmark executable changed during measurement')
        finish_report(report, args.self_check, args.negative_control)
        (output/'comparison.json').write_text(json.dumps(report, indent=2)+'\n')
        if args.negative_control:
            validate_negative(report)
        print('Primitive range pairs accepted:', report['accepted'], 'qualified:', report['qualified'])
        return 0 if report['accepted'] else 1
    except Exception as error:
        (output/'failure.json').write_text(json.dumps({'error': str(error), 'self_check': args.self_check}, indent=2)+'\n')
        raise


if __name__ == '__main__':
    raise SystemExit(main())
