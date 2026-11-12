#!/usr/bin/env python3
"""Same-process warmed bank comparison; procedural hardware coverage only."""
import argparse
import hashlib
import json
import os
import re
import shutil
import platform
from pathlib import Path
import subprocess
from transfer_uint_warm_analysis import analyze

ROOT=Path(__file__).resolve().parents[2]


def git(source,*args):
    return subprocess.check_output(['git','-C',str(source),*args],text=True).strip()


def source_manifest(source,revision,allow_dirty=False):
    source=Path(source).resolve()
    expected=git(ROOT,'rev-parse','--verify',revision+'^{commit}')
    if git(source,'rev-parse','HEAD')!=expected:
        raise ValueError('source worktree does not match requested revision')
    dirty=git(source,'status','--porcelain','--untracked-files=no')
    if dirty and not allow_dirty:
        raise ValueError('qualification requires clean source worktrees')
    files=sorted(set(source.glob('libretro/core_transfer*.[ch]'))|set(source.glob('libretro/core_spv*.[ch]')))
    if not files or not (source/'libretro/core_transfer_uint_vk.c').is_file():
        raise ValueError('missing prepared-bank implementation; incompatible baseline')
    return dict(revision=expected,source=str(source),dirty=bool(dirty),files={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files})


def validate_logs(output):
    identity=None
    for name,reference in (('reference',1),('prepared',0),('forward',0),('reverse',0)):
        lines=(output/(name+'.log')).read_text(encoding='utf-8').splitlines()
        current=[]
        for prefix in ('TRANSFER_WARM_DEVICE ','TRANSFER_WARM_LIMITS ','TRANSFER_PARENT_LIBRARY '):
            matches=[line for line in lines if line.startswith(prefix)]
            if len(matches)!=1:raise ValueError('missing/duplicate device or parent identity')
            patterns={
                'TRANSFER_WARM_DEVICE ':r'TRANSFER_WARM_DEVICE .+ vendor[0-9]+ device[0-9]+ driver[0-9]+ api[0-9]+ native2=1 sampleShading=1 dynamicRendering=0',
                'TRANSFER_WARM_LIMITS ':r'TRANSFER_WARM_LIMITS storage[0-9]+ image[0-9]+',
                'TRANSFER_PARENT_LIBRARY ':r'TRANSFER_PARENT_LIBRARY .+ abi1 packet112'}
            if not re.fullmatch(patterns[prefix],matches[0]):raise ValueError('incompatible device or ABI metadata')
            current.append(matches[0])
        if identity is not None and current!=identity:
            raise ValueError('device/driver/parent identity changed during qualification')
        identity=current
        completed=[line for line in lines if line.startswith('PASS: 90 exact production-bank images, reference='+str(reference)+',')]
        if len(completed)!=1:raise ValueError('missing complete image-oracle run')
    return identity


def qualify(output):
    identity=validate_logs(output)
    forward=analyze(output/'forward.csv',parent=True)
    reverse=analyze(output/'reverse.csv',reverse=True,parent=True)
    if [case['key'] for case in forward['cases']] != [case['key'] for case in reverse['cases']]:
        raise ValueError('different forward/reversed workloads')
    return dict(accepted=forward['accepted'] and reverse['accepted'],identity=identity,forward=forward,reverse=reverse)


def comparison_refs(base,head,pinned=None,self_check=False,configured=None):
    first_parent=git(ROOT,'rev-parse','--verify',base+'^{commit}')
    head_id=git(ROOT,'rev-parse','--verify',head+'^{commit}')
    if self_check:
        if first_parent!=head_id:raise ValueError('self-check requires identical revision labels')
    elif git(ROOT,'rev-parse',head_id+'^1')!=first_parent:
        raise ValueError('base must be the exact first parent')
    selected=first_parent
    if pinned:
        if self_check or not configured:raise ValueError('anchor must be explicitly configured')
        selected=git(ROOT,'rev-parse','--verify',pinned+'^{commit}')
        if selected!=git(ROOT,'rev-parse','--verify',configured+'^{commit}'):
            raise ValueError('pinned reference must match explicit anchor configuration')
    return first_parent,selected,head_id


def execute(command,output,env,logname,timeout=1800):
    with (output/logname).open('w',encoding='utf-8') as log:
        subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=timeout)


def windows_run(output,env):
    execute([env.get('COMSPEC','cmd.exe'),'/d','/c',str(ROOT/'tools/tests/build_transfer_uint_warm_pair.cmd')],output,env,'build.log')
    modules=sorted(output.glob('transfer-*.spv'))
    if len(modules)!=4:raise ValueError('missing/extra probe modules')
    validator=shutil.which('spirv-val')
    distro=env.get('TRANSFER_WARM_WSL_DISTRO')
    with (output/'validation.log').open('w',encoding='utf-8') as log:
        for module in modules:
            if validator:
                command=[validator,'--target-env','vulkan1.0',str(module)]
            elif distro:
                linux=subprocess.check_output(['wsl','-d',distro,'--','wslpath','-u',module.as_posix()],text=True).strip()
                command=['wsl','-d',distro,'--','spirv-val','--target-env','vulkan1.0',linux]
            else:raise ValueError('strict SPIR-V validator required (PATH or explicit TRANSFER_WARM_WSL_DISTRO)')
            subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    for name in ('reference','prepared','forward','reverse'):
        run_env=env.copy()
        run_env.update(TRANSFER_PARENT_LIBRARY=str(output/'parent.dll'),XE_SHADER_BENCH_CSV=str(output/(name+'-init.csv')),XE_SHADER_BENCH_CPU_CSV=str(output/(name+'-init-cpu.csv')))
        if name=='reference':run_env['TRANSFER_REFERENCE']='1'
        elif name=='prepared':run_env['TRANSFER_CORRECTNESS_ONLY']='1'
        else:
            run_env['TRANSFER_GPU_CSV']=str(output/(name+'.csv'))
            if name=='reverse':run_env['TRANSFER_REVERSE_ORDER']='1'
        execute([str(output/'transfer-warm.exe'),str(output/'transfer')],output,run_env,name+'.log')


def toolchain_identity():
    command=['cl','/Bv'] if os.name=='nt' else ['gcc','--version']
    result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
    if not result.stdout.strip():raise ValueError('missing compiler identity')
    if os.name!='nt' and result.returncode:raise ValueError('compiler identity failed')
    return dict(compiler_command=command,compiler_output=result.stdout,compiler_exit=result.returncode,
                host=platform.platform(),machine=platform.machine(),python=platform.python_version())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('base','head','parent-source','head-source','out'):parser.add_argument('--'+name,required=True)
    parser.add_argument('--pinned-reference',help='must match explicitly configured SHADER_PERF_ANCHOR')
    parser.add_argument('--self-check',action='store_true',help='unqualified local harness validation only')
    args=parser.parse_args()
    output=Path(args.out).resolve()
    if output.exists() and any(output.iterdir()):raise ValueError('use a fresh output directory; samples cannot be reused')
    output.mkdir(parents=True,exist_ok=True)
    first_parent,selected,head_id=comparison_refs(args.base,args.head,args.pinned_reference,args.self_check,os.environ.get('SHADER_PERF_ANCHOR'))
    base=source_manifest(args.parent_source,selected,args.self_check)
    head=source_manifest(args.head_source,head_id,args.self_check)
    harness_revision=git(ROOT,'rev-parse','HEAD')
    harness_dirty=bool(git(ROOT,'status','--porcelain'))
    if harness_dirty and not args.self_check:raise ValueError('commit the common harness before qualification')
    harness_files=set((ROOT/'tools/tests').glob('transfer_uint_*'))
    for name in ('run_transfer_uint_warm_tests.sh','build_transfer_uint_warm_pair.cmd','gpu_timestamp_measure.h','vulkan_test_context.h','shader_compile_bench.h','transfer_msaa_image_cases.h','transfer_msaa_spv_fixture.c'):
        harness_files.add(ROOT/'tools/tests'/name)
    harness={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(harness_files) if p.is_file()}
    manifest=dict(toolchain=toolchain_identity(),base=base,head=head,first_parent=first_parent,harness_revision=harness_revision,harness_dirty=harness_dirty,comparison_kind='self_check' if args.self_check else 'pinned_reference' if args.pinned_reference else 'first_parent',harness=harness,self_check=args.self_check,
        driver_environment={name:os.environ.get(name) for name in ('VK_ICD_FILENAMES','VK_DRIVER_FILES','VK_INSTANCE_LAYERS')},
        corpus_version=1,case_states=90,work_groups=180,paired_samples=7,orders=2,
        protocol='reference and candidate banks share one device/context; alternate/reverse actual draws',
        cache='all PSOs ready in-process; driver internal/disk caches NOT certified cold',
        coverage='procedural native2/sample-shading integer ownership only; not cold-play or full renderer signoff')
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    env=os.environ.copy()
    for name in ('TRANSFER_PARENT_LIBRARY','TRANSFER_REFERENCE','TRANSFER_CORRECTNESS_ONLY','TRANSFER_GPU_CSV','TRANSFER_REVERSE_ORDER','MSAA_DYNAMIC_RENDERING','MSAA_DISABLE_SAMPLE_SHADING','XE_SHADER_BENCH_CSV','XE_SHADER_BENCH_CPU_CSV'):
        env.pop(name,None)
    env.update(GPU_TESTS='1',TRANSFER_WARM_PARENT_SOURCE=base['source'],TRANSFER_WARM_HEAD_SOURCE=head['source'],TRANSFER_WARM_OUT=str(output))
    command=['sh',str(ROOT/'tools/tests/run_transfer_uint_warm_tests.sh')]
    try:
        if os.name=='nt':windows_run(output,env)
        else:execute(command,output,env,'run.log')
        report=qualify(output)
        report['self_check']=args.self_check
        (output/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        if not report['accepted']:return 1
    except Exception as error:
        (output/'failure.json').write_text(json.dumps(dict(error=str(error),self_check=args.self_check),indent=2),encoding='utf-8')
        raise
    print('PASS: 180 matched warmed-transfer groups in both orders; self_check='+str(args.self_check))
    return 0


if __name__=='__main__':raise SystemExit(main())
