#!/usr/bin/env python3
"""Check typed vertex inputs, stage rules and emitted SPIR-V interfaces."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

F32='@intrinsic("type", "float", 32)'
I32='@intrinsic("type", "int", 32)'
U32='@intrinsic("type", "uint", 32)'
def vector(count,kind=F32):return f'@intrinsic("vector", {count}, {kind})'
def read(name,location,kind):return f'@intrinsic("shader vertex input", "{name}", {location}, {kind})'
def discard(value):return f'@intrinsic("discard", {value})\n'
OUTPUT='@intrinsic("shader output", @intrinsic("construct", @intrinsic("vector", 4), 0.0, 0.0, 0.0, 1.0))\n'

def interface(path):
 words=struct.unpack('<'+'I'*(path.stat().st_size//4),path.read_bytes());names={};locations={};types={};variables={}
 index=5
 while index<len(words):
  word=words[index];length=word>>16;op=word&65535;values=words[index+1:index+length]
  assert length>0,'Incomplete SPIR-V instruction'
  if op==5:names[values[0]]=struct.pack('<'+'I'*len(values[1:]),*values[1:]).split(b'\0',1)[0].decode()
  elif op==71 and values[1]==30:locations[values[0]]=values[2]
  elif op in (21,22,23,32):types[values[0]]=(op,values[1:])
  elif op==59 and values[2]==1:variables[values[1]]=values[0]
  index+=length
 result={}
 for identity,pointer in variables.items():
  if identity not in locations:continue
  op,pointee=types[pointer];assert op==32
  op,shape=types[pointee[1]];count=1
  if op==23:count=shape[1];op,shape=types[shape[0]]
  result[locations[identity]]=(op,shape,count)
 return result

def main():
 compiler=Path(sys.argv[1]).resolve();validator=shutil.which('spirv-val');assert validator,'spirv-val is required'
 with tempfile.TemporaryDirectory(prefix='dynlex-vertex-inputs-') as temporary:
  folder=Path(temporary)
  def compile(name,source,arguments=('--emit-spirv','--shader-stage=vertex')):
   entry=folder/(name+'.dl');output=folder/(name+'.spv');entry.write_text(source)
   result=subprocess.run([str(compiler),str(entry),*arguments,'-o',str(output)],text=True,capture_output=True,timeout=15)
   return result,output
  for optimization in ('-O0','-O2'):
   source=discard(read('normal',1,vector(3)))+discard(read('color',2,vector(4)))+discard(read('object',3,U32))+discard(read('offset',4,vector(2,I32)))
   source+='@intrinsic("shader output", @intrinsic("shader input", "Position"))\n'
   result,path=compile('mesh'+optimization,source,('--emit-spirv','--shader-stage=vertex',optimization))
   assert result.returncode==0,result.stdout+result.stderr
   subprocess.run([validator,'--target-env','vulkan1.3',str(path)],check=True,capture_output=True,text=True)
   assert interface(path)=={0:(22,(32,),4),1:(22,(32,),3),2:(22,(32,),4),3:(21,(32,0),1),4:(21,(32,1),2)}
  # No implicit location zero when a shader chooses its own position format.
  result,path=compile('own-position',discard(read('position',0,vector(3)))+OUTPUT)
  assert result.returncode==0,result.stdout+result.stderr
  assert interface(path)=={0:(22,(32,),3)}
  result,path=compile('repeated',discard(read('normal',1,vector(3)))*2+OUTPUT)
  assert result.returncode==0,result.stdout+result.stderr
  assert len(interface(path))==1
  invalid=[
   ('empty-name',discard(read('',1,F32)), 'non-empty string literal name'),
   ('negative-location',discard(read('normal','@intrinsic("negate", 1)',F32)), 'non-negative 32-bit compile-time integer'),
   ('wide-location',discard(read('normal',4294967296,F32)), 'non-negative 32-bit compile-time integer'),
   ('fractional-location',discard(read('normal',1.5,F32)), 'non-negative 32-bit compile-time integer'),
   ('float64',discard(read('normal',1,'@intrinsic("type", "float", 64)')), '32-bit numeric scalar or a vector of 2, 3, or 4'),
   ('bool',discard(read('normal',1,'@intrinsic("type", "bool")')), '32-bit numeric scalar or a vector of 2, 3, or 4'),
   ('vector8',discard(read('normal',1,vector(8))), '32-bit numeric scalar or a vector of 2, 3, or 4'),
   ('duplicate-location',discard(read('normal',1,vector(3)))+discard(read('color',1,vector(4))), 'location 1 is assigned to both'),
   ('inconsistent-location',discard(read('normal',1,vector(3)))+discard(read('normal',2,vector(3))), 'uses different locations or types'),
   ('inconsistent-type',discard(read('normal',1,vector(3)))+discard(read('normal',1,vector(4))), 'uses different locations or types'),
   ('default-position-conflict',discard(read('normal',0,vector(3)))+discard('@intrinsic("shader input", "Position")'), 'location 0 is assigned to both'),
  ]
  for name,source,message in invalid:
   result,_=compile(name,source+OUTPUT)
   assert result.returncode>0,(name,result.returncode,result.stderr)
   assert message in result.stdout+result.stderr,(name,result.stdout,result.stderr)
  for stage in ('fragment','compute'):
   result,_=compile(stage,discard(read('normal',1,vector(3))),('--emit-spirv','--shader-stage='+stage))
   assert result.returncode>0 and 'only available in vertex shaders' in result.stdout+result.stderr,result.stderr
  for arguments in ((),('--emit-wasm',)):
   result,_=compile('non-shader',discard(read('normal',1,vector(3))),arguments)
   assert result.returncode>0 and 'only available when emitting SPIR-V' in result.stdout+result.stderr,result.stderr
 print('Typed vertex inputs preserve locations, scalar/vector types and stage/interface errors at both optimizations.')
 return 0
if __name__=='__main__':raise SystemExit(main())
