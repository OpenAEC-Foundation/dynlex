#!/usr/bin/env python3
from __future__ import annotations
import os, re, subprocess, sys, tempfile
from pathlib import Path

SOURCE = '''import lib/std.dl
exposed function increment {a pointer to a 64 bit unsigned integer:value}:
    execute:
        return @intrinsic("atomic fetch add", value, 1 as a 64 bit unsigned integer, "acq_rel")
exposed function load count {a pointer to a 64 bit unsigned integer:value}:
    execute:
        return @intrinsic("atomic load", value, "acquire")
exposed function publish {a pointer to a boolean:ready}:
    execute:
        @intrinsic("atomic store", ready, true, "release")
exposed function is ready {a pointer to a boolean:ready}:
    execute:
        return @intrinsic("atomic load", ready, "acquire")
'''

def run(args, cwd): return subprocess.run(args, cwd=cwd, text=True, capture_output=True, check=False)
def ok(r):
    if r.returncode: raise RuntimeError(f"{r.args} failed:\n{r.stdout}{r.stderr}")
def bad(r, text):
    if r.returncode == 0 or text not in r.stdout + r.stderr: raise RuntimeError(f"expected {text!r}:\n{r.stdout}{r.stderr}")
def symbol(ir, name):
    match = re.search(rf'^define .* @([^ (]*{name.replace(" ", "_")}[^ (]*_callable[^ (]*)\(', ir, re.M)
    if not match: raise RuntimeError(f"missing exposed {name}:\n{ir}")
    return match.group(1)

def main():
    if len(sys.argv) != 2: return 2
    compiler = Path(sys.argv[1]).resolve(); root = Path(__file__).resolve().parent.parent
    if not compiler.is_file(): return 2
    cc = os.environ.get("CC", "cc")
    try:
      with tempfile.TemporaryDirectory(prefix="dynlex-atomics-") as directory:
        d=Path(directory); src=d/'atomics.dl'; src.write_text(SOURCE)
        ll=d/'atomics.ll'; ok(run([str(compiler),str(src),'--emit-llvm','--no-main','-o',str(ll)],root)); ir=ll.read_text()
        for pattern in (r'atomicrmw add ptr %value_val, i64 %\d+ acq_rel, align 8',r'load atomic i64, ptr %value_val acquire, align 8',r'store atomic i8 %atomic_bool_store, ptr %ready_val release, align 1',r'load atomic i8, ptr %ready_val acquire, align 1'):
            if not re.search(pattern, ir): raise RuntimeError(f"missing atomic IR {pattern!r}:\n{ir}")
        inc, load, publish, ready=(symbol(ir,n) for n in ('increment','load count','publish','is ready'))
        caller=d/'caller.c'; caller.write_text(f'''#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
extern uint64_t increment(void *) __asm__("{inc}");
extern uint64_t load_count(void *) __asm__("{load}");
extern void publish(void *) __asm__("{publish}");
extern bool is_ready(void *) __asm__("{ready}");
static _Atomic uint64_t count; static _Atomic bool flag; static int payload;
static void *worker(void *unused) {{ (void)unused; for(int i=0;i<10000;i++) increment(&count); return 0; }}
static void *consumer(void *unused) {{ (void)unused; while(!is_ready(&flag)) {{ }} return payload == 42 ? 0 : (void *)1; }}
int main(void) {{ pthread_t threads[4], reader; for(int i=0;i<4;i++) if(pthread_create(&threads[i],0,worker,0)) return 1; for(int i=0;i<4;i++) if(pthread_join(threads[i],0)) return 2; if(load_count(&count)!=40000) return 3; if(pthread_create(&reader,0,consumer,0)) return 4; payload=42; publish(&flag); void *result=0; if(pthread_join(reader,&result)||result) return 5; return 0; }}
''')
        obj=d/'atomics.o'; exe=d/'caller'
        for opt in ('-O0','-O2','-O3'):
            ok(run([str(compiler),str(src),'--emit-object','--no-main',opt,'-o',str(obj)],root)); ok(run([cc,'-std=c11','-O2','-pthread',str(caller),str(obj),'-o',str(exe)],root)); ok(run([str(exe)],root))
        negatives=[('@intrinsic("atomic load", 1, "relaxed")\n','Atomic operation requires a pointer'),('@intrinsic("atomic load", 0 as a pointer, "release")\n','Atomic memory order is invalid'),('@intrinsic("atomic fetch add", 0 as a pointer, 1, "relaxed")\n','Atomic operation value type must exactly match')]
        for index,(text,error) in enumerate(negatives):
            path=d/f'bad{index}.dl'; path.write_text('import lib/std.dl\n'+text); bad(run([str(compiler),str(path),'--emit-llvm'],root),error)
        target=d/'target.dl'; target.write_text('import lib/std.dl\nset value to 0\n@intrinsic("atomic load", the address of value, "relaxed")\n')
        for args in (('--emit-wasm',),('--emit-spirv','--shader-stage=fragment')): bad(run([str(compiler),str(target),*args],root),'Atomic operations are only available when emitting native CPU code')
    except RuntimeError as error: print(error,file=sys.stderr); return 1
    return 0
if __name__ == '__main__': raise SystemExit(main())
