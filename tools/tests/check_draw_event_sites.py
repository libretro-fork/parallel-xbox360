"""Check direct core driver creation calls cannot bypass stats event sites.
This source check complements dynamic counters; it is not a binary interposer
and does not certify coverage of future aliases or complete gameplay behavior.
"""
import re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
API={"vkCreateShaderModule":"XE_DRAW_MODULE","vkCreateGraphicsPipelines":"XE_DRAW_GRAPHICS_PSO",
     "vkCreateComputePipelines":"XE_DRAW_COMPUTE_PSO","CreateGraphicsPipelineState":"XE_DRAW_GRAPHICS_PSO",
     "CreateComputePipelineState":"XE_DRAW_COMPUTE_PSO","CreatePipelineState":"XE_DRAW_GRAPHICS_PSO"}
def masked(s):
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|(?<![\w])\'(?:\\.|[^\'\\\n])*\'',
        lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),s)
def closing(s,start):
    depth=0
    for i in range(start,len(s)):
        if s[i]=='(':depth+=1
        elif s[i]==')':
            depth-=1
            if not depth:return i+1
    raise ValueError('unclosed call')
def inspect(s):
    text=masked(s);sites=[];bad=[]
    for match in re.finditer(r'\bXE_DRAW_CALL\s*\(',text):
        start=text.index('(',match.start());end=closing(text,start);depth=0;commas=[]
        for i in range(start,end):
            if text[i]=='(':depth+=1
            elif text[i]==')':depth-=1
            elif text[i]==',' and depth==1:commas.append(i)
        if len(commas)==2:sites.append((commas[1]+1,end-1,text[commas[0]+1:commas[1]].strip()))
    for match in re.finditer(r'\b('+'|'.join(API)+r')\s*\(',text):
        end=closing(text,text.index('(',match.start()))
        if not any(a<=match.start() and end<=b and event==API[match[1]] for a,b,event in sites):
            bad.append((text.count('\n',0,match.start())+1,match[1]))
    return bad,len(list(re.finditer(r'\b('+'|'.join(API)+r')\s*\(',text)))
def main():
    total=0;fail=[]
    for folder in ['src/xenia/gpu','src/xenia/ui','libretro']:
        for p in (ROOT/folder).rglob('*'):
            if p.suffix not in ['.cc','.cpp','.h','.c']:continue
            bad,count=inspect(p.read_text(encoding='utf-8'));total+=count
            fail.extend((str(p.relative_to(ROOT)),line,api) for line,api in bad)
    if not total:raise ValueError('empty API-site corpus')
    sample='return XE_DRAW_CALL(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_GRAPHICS_PSO,dfn.vkCreateGraphicsPipelines(a,b,f(c,d)));'
    assert not inspect(sample)[0]
    assert inspect(sample.replace('XE_DRAW_GRAPHICS_PSO','XE_DRAW_MODULE'))[0]
    assert inspect('return dfn.vkCreateGraphicsPipelines(a,b);')[0]
    assert inspect('return XE_DRAW_CALL(K,XE_DRAW_MODULE,device->functions()).vkCreateShaderModule(a,b);')[0]
    assert not inspect('// vkCreateShaderModule(fake)\nconst char* s="CreatePipelineState(fake)";')[1]
    assert inspect("unsigned x=1'000; return dfn.vkCreateGraphicsPipelines(a,b);")[0]
    if fail:
        for file,line,api in fail:print('%s:%s: uncovered/mistagged %s'%(file,line,api),file=sys.stderr)
        return 1
    print('PASS: %d direct core driver creation sites instrumented; unwrapped/wrong-event negative controls rejected'%total)
    return 0
if __name__=='__main__':sys.exit(main())
