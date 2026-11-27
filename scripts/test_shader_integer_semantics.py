#!/usr/bin/env python3
"""Check signed GPU operations through import-free arithmetic fixtures."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


def verify_types(path):
    words=struct.unpack('<'+'I'*(path.stat().st_size//4),path.read_bytes())
    instructions=[]
    position=5
    while position<len(words):
        length,opcode=words[position]>>16,words[position]&65535
        assert length>0
        instructions.append((opcode,words[position+1:position+length]))
        position+=length
    types={values[0]:(opcode,values[1:]) for opcode,values in instructions if 19<=opcode<=39}
    values={}
    functions=False
    for opcode,args in instructions:
        functions=functions or opcode==54
        if (functions or opcode==59 or 41<=opcode<=52) and len(args)>=2 and args[0] in types:
            values[args[1]]=args[0]
    def sign(kind):
        opcode,args=types[kind]
        if opcode==23:return sign(args[0])
        assert opcode==21,(opcode,args)
        return args[1]
    for opcode,args in instructions:
        assert opcode != 138, "Signed remainder must use portable division/product/subtraction"
        if opcode==111: assert sign(values[args[2]])==1, 'Signed-to-float consumed an unsigned value'
        if opcode==112: assert sign(values[args[2]])==0, 'Unsigned-to-float consumed a signed value'
        if opcode in [110,114,126,135,138,139,195]:
            assert sign(args[0])==1, f'Signed operation {opcode} produced an unsigned value'
        if opcode in [109,113,134,137,194]:
            assert sign(args[0])==0, f'Unsigned operation {opcode} produced a signed value'
        if opcode in [114,126,135,138,139,173,175,177,179,195]:
            assert sign(values[args[2]])==1, f'Signed operation {opcode} consumed an unsigned value'


def main():
    compiler=Path(sys.argv[1]).resolve()
    root=Path(__file__).resolve().parents[1]
    validator=shutil.which('spirv-val')
    assert validator,'spirv-val is required'
    with tempfile.TemporaryDirectory(prefix='dynlex shader integers ') as directory:
        # Exercise the CLI from an escaped file URL as well as native Windows paths.
        generator=Path(directory)/'shader_arithmetic_cases.mjs'
        shutil.copy2(root/'tests/web/shader_arithmetic_cases.mjs',generator)
        shutil.copy2(root/'tests/web/shader_arithmetic_cases.json',generator.with_suffix('.json'))
        fixtures=json.loads(subprocess.check_output(['node',str(generator)],text=True))
        source,binary=Path(directory)/'main.dl',Path(directory)/'main.spv'
        for fixture in fixtures:
            source.write_text(fixture['source'])
            for level in ['-O0','-O2']:
                result=subprocess.run([str(compiler),str(source),'--emit-spirv','--shader-stage=fragment',level,'-o',str(binary)],capture_output=True,text=True,timeout=15)
                assert result.returncode==0 and not result.stderr,(fixture['name'],level,result.stderr)
                validation=subprocess.run([validator,'--target-env','vulkan1.3',str(binary)],capture_output=True,text=True)
                assert validation.returncode==0,(fixture['name'],level,validation.stderr)
                verify_types(binary)
    print('GPU integer conversions, division, remainders, shifts and extrema preserve their signed interpretation at both optimizations.')


if __name__=='__main__':main()
