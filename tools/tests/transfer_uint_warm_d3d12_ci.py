#!/usr/bin/env python3
"""Test-only D3D12 parent/candidate GPU gate; no runtime dependency."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path
from transfer_uint_warm_ci import ROOT,git,comparison_refs,execute,toolchain_identity
from transfer_uint_warm_analysis import analyze


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_record(path,revision,allow_dirty=False):
    source=Path(path).resolve()
    if git(source,'rev-parse','HEAD')!=revision:
        raise ValueError('producer source revision mismatch')
    dirty=bool(git(source,'status','--porcelain'))
    if dirty and not allow_dirty:
        raise ValueError('producer source must be clean')
    files=set(source.glob('libretro/core_transfer*.[ch]'))
    files.update(source.glob('src/xenia/gpu/dxil/**/*.[ch]'))
    if not (source/'src/xenia/gpu/dxil/edram_transfer.c').is_file():
        raise ValueError('missing native parent producer')
    return dict(source=str(source),revision=revision,dirty=dirty,prepared=(source/'libretro/core_transfer_uint_d3d12.c').is_file(),
                files={str(p.relative_to(source)):digest(p) for p in sorted(files) if p.is_file()})


def build_record(path,source):
    build=Path(path).resolve()
    cache={}
    for line in (build/'CMakeCache.txt').read_text(encoding='utf-8').splitlines():
        found=re.fullmatch(r'([^/#][^:]*):[^=]+=(.*)',line)
        if found:cache[found[1]]=found[2]
    if not cache.get('CMAKE_HOME_DIRECTORY') or Path(cache['CMAKE_HOME_DIRECTORY']).resolve()!=Path(source).resolve():
        raise ValueError('archive build belongs to another source directory')
    keys=('CMAKE_BUILD_TYPE','CMAKE_C_FLAGS','CMAKE_C_FLAGS_RELEASE','CMAKE_CXX_FLAGS',
          'CMAKE_CXX_FLAGS_RELEASE','CMAKE_C_COMPILER','CMAKE_CXX_COMPILER','XENIA_ENABLE_LTO')
    if any(k not in cache for k in keys):raise ValueError('missing build options')
    requested={key:cache[key] for key in ('CMAKE_C_COMPILER','CMAKE_CXX_COMPILER')}
    for key in requested:
        compiler=shutil.which(cache[key])
        if not compiler:raise ValueError('native compiler is unavailable')
        cache[key]=os.path.normcase(str(Path(compiler).resolve()))
    archive=build/'obj/Windows/xenia-native-dxil.lib'
    return dict(build=str(build),archive=str(archive),archive_sha256=digest(archive),compiler_requested=requested,compiler_sha256={key:digest(cache[key]) for key in requested},options={k:cache[k] for k in keys})


def one_line(lines,prefix):
    found=[line for line in lines if line.startswith(prefix)]
    if len(found)!=1:raise ValueError('missing/duplicate '+prefix)
    return found[0]


def validate_log(path,reverse,negative,parent,prepared):
    lines=Path(path).read_text(encoding='utf-8').splitlines()
    adapter=one_line(lines,'MSAA_D3D12_ADAPTER ')
    if not re.fullmatch(r'MSAA_D3D12_ADAPTER hardware1 vendor[0-9]+ device[0-9]+ subsystem[0-9]+ revision[0-9]+',adapter):
        raise ValueError('a real hardware adapter is required')
    driver=one_line(lines,'D3D12_WARM_DRIVER ')
    if not re.fullmatch(r'D3D12_WARM_DRIVER [0-9a-fA-F]{8}:[0-9a-fA-F]{8}',driver):
        raise ValueError('missing driver version')
    if driver=='D3D12_WARM_DRIVER 00000000:00000000':raise ValueError('unknown driver version')
    identity=one_line(lines,'D3D12_WARM_ID ')
    found=re.fullmatch(r'D3D12_WARM_ID (luid[0-9a-fA-F]{8}:[0-9a-fA-F]{8}) dynamic_control([01]) reverse([01])',identity)
    if not found or int(found[2])!=int(negative) or int(found[3])!=int(reverse):
        raise ValueError('wrong order/control/device identity')
    if found[1]=='luid00000000:00000000':raise ValueError('unknown hardware adapter LUID')
    if one_line(lines,'MSAA_D3D12 hardware')!='MSAA_D3D12 hardware1 native2=1':
        raise ValueError('native2 coverage is missing')
    producer=one_line(lines,'D3D12_PARENT_LIBRARY ')
    found_parent=re.fullmatch(r'D3D12_PARENT_LIBRARY abi1 kind([12]) packet(4|112) validated_modules([0-9]+) bytes([0-9]+) (.+)',producer)
    if not found_parent or (int(found_parent[1]),int(found_parent[2]))!=((2,112) if prepared else (1,4)):
        raise ValueError('wrong parent ABI or implementation')
    if int(found_parent[3])<1 or int(found_parent[4])<1 or Path(found_parent[5]).resolve()!=Path(parent).resolve():
        raise ValueError('wrong/unvalidated parent producer')
    complete=one_line(lines,'PASS: 90 warmed D3D12 cases,')
    if complete!='PASS: 90 warmed D3D12 cases, both image implementations, exact GPU work counts; 2880 raw samples':
        raise ValueError('incomplete images or GPU work coverage')
    return dict(adapter=adapter,driver=driver,luid=found[1],parent=producer)


def qualify(output,prepared,negative=False):
    output=Path(output)
    forward_id=validate_log(output/'forward.log',False,negative,output/'parent.dll',prepared)
    reverse_id=validate_log(output/'reverse.log',True,negative,output/'parent.dll',prepared)
    if forward_id!=reverse_id:raise ValueError('device or producer changed between orders')
    forward=analyze(output/'forward.csv',parent=True)
    reverse=analyze(output/'reverse.csv',reverse=True,parent=True)
    if [r['key'] for r in forward['cases']]!=[r['key'] for r in reverse['cases']]:
        raise ValueError('different forward/reverse corpus')
    return dict(accepted=forward['accepted'] and reverse['accepted'],identity=forward_id,forward=forward,reverse=reverse)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('base','head','parent-source','head-source','parent-build','head-build','utilities','out'):
        parser.add_argument('--'+name,required=True)
    parser.add_argument('--pinned-reference',help='must match explicit SHADER_PERF_ANCHOR')
    parser.add_argument('--self-check',action='store_true',help='unqualified local harness validation')
    parser.add_argument('--negative-control',action='store_true',help='test-only dynamic-shader regression')
    args=parser.parse_args()
    if os.name!='nt':raise ValueError('MSVC/Windows D3D12 hardware runner required')
    output=Path(args.out).resolve()
    if output.exists() and any(output.iterdir()):raise ValueError('fresh output directory required')
    output.mkdir(parents=True,exist_ok=True)
    first,selected,head=comparison_refs(args.base,args.head,args.pinned_reference,args.self_check,os.environ.get('SHADER_PERF_ANCHOR'))
    parent=source_record(args.parent_source,selected,args.self_check)
    candidate=source_record(args.head_source,head,args.self_check)
    if not candidate['prepared']:raise ValueError('candidate has no prepared D3D12 bank')
    before=[build_record(args.parent_build,parent['source']),build_record(args.head_build,candidate['source'])]
    if before[0]['options']!=before[1]['options']:raise ValueError('native build options differ')
    harness_dirty=bool(git(ROOT,'status','--porcelain'))
    if harness_dirty and not args.self_check:raise ValueError('commit the common harness before qualification')
    utilities=Path(args.utilities).resolve()
    utility_files=[Path(str(utilities)+suffix) for suffix in ('-vertex.dxil','-fill.dxil','-probe-s0.dxil','-probe-s1.dxil')]
    files=set((ROOT/'tools/tests').glob('transfer_uint_*d3d12*'))
    files.update(ROOT/'tools/tests'/name for name in ('transfer_msaa_image_d3d12_test.c','transfer_msaa_image_cases.h',
        'transfer_uint_image_cases.h','transfer_uint_d3d12_image.h','gpu_timestamp_measure.h','transfer_test_adapter.h',
        'core_dxil_validation_bridge.cc','transfer_uint_warm_analysis.py','transfer_uint_warm_ci.py',
        'build_transfer_uint_warm_d3d12.cmd','build_transfer_uint_d3d12_revision.cmd'))
    manifest=dict(base=parent,head=candidate,first_parent=first,builds=before,toolchain=toolchain_identity(),
        harness_revision=git(ROOT,'rev-parse','HEAD'),harness_dirty=harness_dirty,self_check=args.self_check,
        comparison_kind='self_check' if args.self_check else 'pinned_reference' if args.pinned_reference else 'first_parent',
        harness={str(p.relative_to(ROOT)):digest(p) for p in sorted(files) if p.is_file()},
        utilities={str(p):digest(p) for p in utility_files},negative_control=args.negative_control,
        cache='all PSOs prepared before timing; driver internal/disk caches NOT certified cold',
        coverage='90 procedural integer native2 cases, seven matched samples, 180 groups both orders; not gameplay signoff')
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    env=os.environ.copy()
    for name in ('TRANSFER_PARENT_LIBRARY','TRANSFER_GPU_CSV','TRANSFER_REVERSE_ORDER','TRANSFER_WARM_DYNAMIC_CONTROL','MSAA_HARDWARE'):
        env.pop(name,None)
    env.update(TRANSFER_D3D12_PARENT_SOURCE=parent['source'],TRANSFER_D3D12_PARENT_BUILD=before[0]['build'],
        TRANSFER_D3D12_HEAD_SOURCE=candidate['source'],UINT_IMAGE_BUILD=before[1]['build'],TRANSFER_D3D12_WARM_OUT=str(output))
    try:
        for index,record in enumerate(before):
            execute(['cmake','--build',record['build'],'--target','xenia-native-dxil','--parallel','3'],output,env,'native-build-'+str(index)+'.log')
        before=[build_record(args.parent_build,parent['source']),build_record(args.head_build,candidate['source'])]
        manifest['builds']=before
        for script in ('build_transfer_uint_d3d12_revision.cmd','build_transfer_uint_warm_d3d12.cmd'):
            execute([env.get('COMSPEC','cmd.exe'),'/d','/c',str(ROOT/'tools/tests'/script)],output,env,script+'.log')
        if not env.get('XE_TEST_DXIL_VALIDATOR'):
            env['XE_TEST_DXIL_VALIDATOR']=str(Path(env['WINDOWSSDKDIR'])/'bin'/env['WINDOWSSDKVERSION'].rstrip('\\/')/'x64/dxil.dll')
        if not Path(env['XE_TEST_DXIL_VALIDATOR']).is_file():raise ValueError('missing strict Windows SDK validator')
        manifest['binaries']={name:digest(output/name) for name in ('parent.dll','abi-test.exe','warm-test.exe')}
        manifest['validator_sha256']=digest(env['XE_TEST_DXIL_VALIDATOR'])
        (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
        env.update(MSAA_HARDWARE='1',TRANSFER_PARENT_LIBRARY=str(output/'parent.dll'))
        if args.negative_control:env['TRANSFER_WARM_DYNAMIC_CONTROL']='1'
        for name in ('forward','reverse'):
            env['TRANSFER_GPU_CSV']=str(output/(name+'.csv'))
            if name=='reverse':env['TRANSFER_REVERSE_ORDER']='1'
            execute([str(output/'warm-test.exe'),str(utilities)],output,env,name+'.log')
        if before!=[build_record(args.parent_build,parent['source']),build_record(args.head_build,candidate['source'])]:
            raise ValueError('native archive/options changed during measurement')
        if parent!=source_record(args.parent_source,selected,args.self_check) or candidate!=source_record(args.head_source,head,args.self_check):
            raise ValueError('producer sources changed during measurement')
        report=qualify(output,parent['prepared'],args.negative_control)
        report.update(self_check=args.self_check,negative_control=args.negative_control,qualified=not args.self_check and report['accepted'])
        (output/'comparison.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print('D3D12 warmed groups:',len(report['forward']['cases']),'accepted:',report['accepted'],'self_check:',args.self_check)
        return 0 if report['accepted'] else 1
    except Exception as error:
        (output/'failure.json').write_text(json.dumps(dict(error=str(error),self_check=args.self_check),indent=2)+'\n',encoding='utf-8')
        raise


if __name__=='__main__':raise SystemExit(main())
