#!/usr/bin/env python3
"""Strict test-only D3D12 color throughput analysis; never accepts small controls."""
import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics
from transfer_uint_warm_analysis import distribution

FORMATS=(0,1,2,3,4,5,14,15)
SCALES=((1,1),(3,5),(4,4))
FIELDS=('hardware control self_check reverse scale_x scale_y native_class source dest '
        'value_convert source_width source_height source_pitch dest_pitch width height repeat '
        'trial order reference frequency start end host_seconds ia_vertices ia_primitives '
        'vs_invocations ps_invocations').split()


def require(condition,message):
    if not condition:raise ValueError(message)


def expected_cases(controls_only=False):
    if controls_only:return {(1,1,0,0,1,0,r) for r in (2,8)}
    pairs=[(s,d,0) for s in FORMATS for d in FORMATS if s!=d and not(s>=4 and d>=4)]
    pairs.append((3,0,1))
    return {(sx,sy,n,s,d,c,r) for sx,sy in SCALES for n in (range(4) if sx!=1 else (0,))
            for s,d,c in pairs for r in (64,512)}


def analyze(path,reverse=False,self_check=False,controls_only=False,hardware=True):
    expected=expected_cases(controls_only);trials=2 if controls_only else 8
    expected_count=len(expected)*trials*2;groups={};count=0;frequencies=set()
    with Path(path).open(newline='',encoding='utf-8') as source:
        reader=csv.DictReader(source)
        require(reader.fieldnames==FIELDS,'wrong color D3D12 schema')
        for row in reader:
            count+=1;require(count<=expected_count and None not in row,'excess records/columns')
            numbers={}
            for field in FIELDS:
                if field=='host_seconds':continue
                require(isinstance(row[field],str) and re.fullmatch(r'[0-9]+',row[field]),'noninteger counter')
                numbers[field]=int(row[field]);require(numbers[field]<1<<64,'counter overflow')
            require(tuple(numbers[x] for x in ('hardware','control','self_check','reverse'))==
                    tuple(map(int,(hardware,controls_only,self_check,reverse))),'substituted producer/control mode')
            key=tuple(numbers[x] for x in ('scale_x','scale_y','native_class','source','dest','value_convert','repeat'))
            require(key in expected,'wrong color corpus')
            sx,sy,n,s,d,converted,repeat=key
            base_width,base_height=(160,16) if controls_only else (2560,64)
            sw=base_width*(1 if n&1 else sx);sh=base_height*(1 if n&1 else sy)
            dw=base_width*(1 if n&2 else sx);dh=base_height*(1 if n&2 else sy)
            if not controls_only:sw>>=int(s in (5,15));dw>>=int(d in (5,15))
            pitch=4 if controls_only else 32
            require(tuple(numbers[x] for x in ('source_width','source_height','width','height','source_pitch','dest_pitch'))==
                    (sw,sh,dw,dh,pitch,pitch),'wrong image extent or EDRAM pitch')
            trial,order,ref=(numbers[x] for x in ('trial','order','reference'))
            require(trial<trials and order in (0,1) and ref in (0,1),'bad paired sample')
            require(ref==int((((order^(trial&1))==0)!=reverse)),'wrong paired order')
            require(numbers['ia_vertices']==6*repeat and numbers['ia_primitives']==2*repeat and
                    numbers['vs_invocations']>0 and numbers['ps_invocations']>=dw*dh*repeat,
                    'inactive or missing GPU work')
            frequency,start,end=(numbers[x] for x in ('frequency','start','end'))
            require(frequency>0 and end>start,'invalid/unavailable GPU timestamps')
            frequencies.add(frequency)
            require(isinstance(row['host_seconds'],str),'missing host interval')
            host=float(row['host_seconds']);seconds=(end-start)/frequency
            require(math.isfinite(host) and math.isfinite(seconds) and 0<seconds<=host<3600,
                    'invalid GPU/host interval')
            slots=groups.setdefault(key,{})
            require((trial,ref) not in slots,'duplicate paired slot')
            slots[trial,ref]=seconds
    require(count==expected_count and set(groups)==expected,'missing color samples')
    require(len(frequencies)==1,'inconsistent timestamp clock')
    results=[];failures=[];scaling=[]
    for key,slots in sorted(groups.items()):
        require(set(slots)=={(t,r) for t in range(trials) for r in (0,1)},'incomplete pairs')
        if controls_only:continue
        # Retain trial zero in raw data. Match the existing declared Vulkan
        # warm protocol: seven primed pairs (trials 1..7), never retry/filter.
        pairs=[(slots[t,1],slots[t,0]) for t in range(1,8)]
        ratios=[b/a for a,b in pairs];deltas=[(b-a)/key[-1] for a,b in pairs]
        regression=(statistics.median(ratios)>1.05 and statistics.median(deltas)>1e-7 and
                    sum(b/a>1.05 and (b-a)/key[-1]>1e-7 for a,b in pairs)>=5)
        item=dict(key=key,paired_median_ratio=statistics.median(ratios),
                  median_seconds_per_draw_delta=statistics.median(deltas),repeatable_regression=regression,
                  reference_distribution=distribution([a for a,b in pairs]),
                  candidate_distribution=distribution([b for a,b in pairs]))
        results.append(item)
        if regression:failures.append(item)
        if key[-1]==64:
            larger=groups[key[:-1]+(512,)]
            for ref in (0,1):
                ratio=statistics.median(larger[t,ref] for t in range(1,8))/statistics.median(slots[t,ref] for t in range(1,8))
                scaling.append(dict(key=key[:-1],reference=ref,ratio=ratio,valid=ratio>=2))
    passed=not failures and all(x['valid'] for x in scaling)
    return dict(corpus_version=1,sample_count=count,controls_only=controls_only,self_check=self_check,
                reverse_order=reverse,hardware=hardware,protocol_valid=True,
                controls_valid=passed if (controls_only or self_check) else False,
                accepted=passed and hardware and not controls_only and not self_check,
                cases=results,failures=failures,work_scaling=scaling,
                threshold='unchanged color protocol: paired median >5% and >100ns/draw, >=5/7 exceedances; work512/work64 >=2',
                coverage='405 canonical color/native-scale cases, 810 workload groups; aliases validated separately; no cold-gameplay signoff')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('csv');parser.add_argument('--out',required=True)
    parser.add_argument('--reverse',action='store_true');parser.add_argument('--self-check',action='store_true')
    parser.add_argument('--controls-only',action='store_true');parser.add_argument('--software',action='store_true')
    args=parser.parse_args();report=analyze(args.csv,args.reverse,args.self_check,args.controls_only,not args.software)
    Path(args.out).write_text(json.dumps(report,indent=2)+'\n')
    print('D3D12 color rows:',report['sample_count'],'regressions:',len(report['failures']))
    raise SystemExit(0 if report['accepted'] or report['controls_valid'] else 1)
