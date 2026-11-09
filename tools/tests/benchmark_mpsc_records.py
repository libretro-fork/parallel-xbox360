"""Reproducible timer-admission transport comparison; run inside VS x64 tools.
Historical C++ dependency is extracted from Git to private test output only.
No emulator/game runs and no installed cores or caches are touched.
"""
import argparse,json,os,statistics,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--reference',required=True);p.add_argument('--build',default='build/native-msvc');p.add_argument('--out',default='build/tests/mpsc-benchmark');a=p.parse_args()
r=Path(__file__).resolve().parents[2];out=(r/a.out).resolve();out.mkdir(parents=True,exist_ok=True)
def run(args):
 z=subprocess.run([str(x) for x in args],cwd=r,text=True,capture_output=True,check=True);return z.stdout+z.stderr
ref=run(['git','rev-parse',a.reference]).strip()
for f in run(['git','ls-tree','-r','--name-only',ref,'third_party/disruptorplus/include']).splitlines():
 rel=Path(f).relative_to('third_party/disruptorplus/include');target=out/'reference'/rel;target.parent.mkdir(parents=True,exist_ok=True)
 target.write_bytes(subprocess.check_output(['git','show',ref+':'+f],cwd=r))
common=['/nologo','/MD','/O2','/DHAVE_THREADS','/DHAVE_THREAD_STORAGE','/DRETRO_ATOMIC_FORCE_MSVC','/I.','/Ithird_party/libretro-common/include']
commands=[['cl',*common,'/TC','/W4','/WX','/c','tools/tests/core_mpsc_records_bench.c','/Fo'+str(out/'driver.obj')],
 ['cl',*common,'/TC','/W4','/WX','/c','libretro/core_mpsc_records.c','/Fo'+str(out/'queue.obj')],
 ['cl',*common,'/std:c++20','/EHsc','/I'+str(out/'reference'),'/c','tools/tests/mpsc_records_bench_bridge.cc','/Fo'+str(out/'bridge.obj')],
 ['cl','/nologo','/MD',out/'driver.obj',out/'bridge.obj',out/'queue.obj',r/a.build/'obj/Windows/retro-common.lib','winmm.lib','ws2_32.lib','bcrypt.lib','/Fe'+str(out/'bench.exe')]]
(out/'build.log').write_text(''.join(run(x) for x in commands))
report={'reference':ref,'capacity':512,'unit_bytes':16,'records_per_producer':200000,'pairs':7,'batches_per_process':5,'cases':{}}
for workers in [1,4]:
 samples={0:[],1:[]}
 for pair in range(7):
  for v in ([0,1] if pair%2==0 else [1,0]):
   data=run([out/'bench.exe',v,workers,1,200000,0]);(out/('w%d-v%d-pair%d.txt'%(workers,v,pair))).write_text(data)
   values=[int(x.split(',')[-1])*1000/(workers*200000) for x in data.splitlines() if x.startswith('SAMPLE,')];assert len(values)==5
   samples[v].append({'raw_ns_per_record':values,'median':statistics.median(values)})
 med={v:statistics.median(x['median'] for x in samples[v]) for v in samples}
 report['cases'][str(workers)]={'samples':samples,'median_ns_per_record':med,'ratio':med[1]/med[0]}
 print(workers,med,flush=True)
(out/'results.json').write_text(json.dumps(report,indent=2))
