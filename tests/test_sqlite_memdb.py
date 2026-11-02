#!/usr/bin/env python3
"""Unmodified SQLite/libc component test: cold memdb, exhaustive rowids, RO image.

No driver/ISA execution or full rung-4 conformance is inferred from this test.
SQLite's compiler vectorisation remains enabled. The native checker accepts
the source-linked LLVM operations; whole-program normalisation is a consumer
obligation tested by the conformance repo, not emulated here.
"""
import hashlib
import importlib.util
import os
from pathlib import Path
import sqlite3
import subprocess
import zipfile
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('varargs_support',ROOT/'tests/varargs_support.py')
support=importlib.util.module_from_spec(spec); spec.loader.exec_module(support)
TMP=Path(os.environ['TMPDIR'])/'sqlite-memdb'; TMP.mkdir(parents=True,exist_ok=True)
archive=ROOT/'vendor/sqlite-amalgamation-3540100.zip'
assert hashlib.sha3_256(archive.read_bytes()).hexdigest()=='92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30'
with zipfile.ZipFile(archive) as z:
    for name in ('sqlite3.c','sqlite3.h'):
        (TMP/name).write_bytes(z.read('sqlite-amalgamation-3540100/'+name))
N=1024
names=['Anne','Brandon','Carl','Diana','Evan','Fran','George','Hannah']
cities=['Berlin','London','Madrid','Paris','Boston','Lima','Oslo','Bern']
rows=[(i,names[i%8],cities[(i//3)%8],18+i*13%60,i*37%1000-100) for i in range(1,N+1)]
def expected(i):
    if not 1<=i<=N: return 0
    _,name,city,age,score=rows[i-1]; z=score*7+age
    rem=z%11 if z>=0 else -((-z)%11)
    return int('an' in name.lower() and city[0].upper() in 'BLM' and 25<=age<=60 and rem==3)
dbpath=TMP/'image.sqlite'; dbpath.unlink(missing_ok=True)
db=sqlite3.connect(dbpath)
db.execute('PRAGMA page_size=4096')
db.execute('CREATE TABLE people(id INTEGER PRIMARY KEY,name TEXT,city TEXT,age INTEGER,score INTEGER)')
db.executemany('INSERT INTO people VALUES(?,?,?,?,?)',rows); db.commit(); db.close()
data=dbpath.read_bytes(); assert data[:16]==b'SQLite format 3\0' and len(data)%4096==0
size=len(data); region=1<<(size-1).bit_length(); data+=b'\0'*(region-size)
h=14695981039346656037
for b in data: h=((h^b)*1099511628211)&((1<<64)-1)
(TMP/'image.h').write_text('#define IMAGE_ROWS 1024\n#define IMAGE_SIZE '+str(size)+'\n#define IMAGE_HASH UINT64_C('+str(h)+')\nstatic const unsigned char image[]={'+','.join(map(str,data))+'};\n')
flags=['-DSQLITE_THREADSAFE=0','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_OS_OTHER=1',
       '-DSQLITE_TEMP_STORE=3','-DSQLITE_DEFAULT_MEMSTATUS=0','-DSQLITE_OMIT_SHARED_CACHE',
       '-DSQLITE_OMIT_DEPRECATED','-DSQLITE_DQS=0']
def run(*args):
    try: return subprocess.check_output(list(map(str,args)),stderr=subprocess.PIPE,timeout=180)
    except subprocess.CalledProcessError as e:
        print(e.stderr.decode(errors='replace')); raise
for major in (18,23):
    llvm=Path(f'/usr/lib/llvm-{major}/bin'); dist=ROOT/f'dist/llvm{major}'; objects=[]
    for source in (TMP/'sqlite3.c',ROOT/'tests/sqlite_nofs.c',ROOT/'tests/sqlite_memdb.c'):
        bc=TMP/f'{source.stem}-{major}.bc'; objects.append(bc)
        run(llvm/'clang','--target=x86_64-unknown-linux-musl','-O2','-flto',
            '-nostdinc','-isystem',dist/'musl-sysroot/include','-I',TMP,*flags,'-emit-llvm','-c',source,'-o',bc)
    app=TMP/f'app-{major}.bc'; linked=TMP/f'linked-{major}.bc'; final=TMP/f'postlink-{major}.bc'
    # Link every application translation unit before selecting the runtime
    # closure. --only-needed on the TU link would discard unreferenced main.
    run(llvm/'llvm-link',*objects,'-o',app)
    run(llvm/'llvm-link',app,dist/'bennett-runtimes.bc','--only-needed','-o',linked)
    run(llvm/'opt','-passes=internalize,lto<O3>','-internalize-public-api-list=main,query_on_image',linked,'-o',final)
    assert 'main' in {l.split()[-1] for l in run(llvm/'llvm-nm','--defined-only',final).decode().splitlines() if l.split()}
    undefined={l.split()[-1] for l in run(llvm/'llvm-nm','--undefined-only',final).decode().splitlines() if l.split()}
    assert undefined<={'__bennett_nz_alloc','__bennett_nz_free','__bennett_nz_fault','__bennett_effect_console_write'},undefined
    # N21 scalarisation follows the vectorising stock frontend and LTO.
    scalar=TMP/f'scalar-{major}.bc'
    options=['-passes=scalarizer','-scalarize-load-store'] if major==18 else ['-passes=scalarizer<load-store>']
    run(llvm/'opt',*options,final,'-o',scalar)
    normal=TMP/f'n23-{major}.bc'
    support.materialise(major,scalar,normal)
    layout=TMP/f'layout-{major}.ll'; exe=TMP/f'query-{major}'
    # Comparison-only test transport for console calls already inlined by
    # LTO. The production publication still never defines effect markers.
    # This test adapter is not an R effect tape or driver materialisation.
    layout.write_text('''@__bennett_rt_native_layout = constant {i64,i64,i64} {i64 1,i64 536875008,i64 1048576}, align 8
declare void @__bennett_rt_native_check(ptr,i64)
declare void @__bennett_rt_native_console_write(ptr,i64)
define void @__bennett_effect_console_write(ptr %p,i64 %n) {
  call void @__bennett_rt_native_check(ptr %p,i64 %n)
  call void @__bennett_rt_native_console_write(ptr %p,i64 %n)
  ret void
}
''')
    run(ROOT/'scripts/native-check-link',major,normal,exe,layout)
    assert not run(llvm/'llvm-nm','--undefined-only',exe).strip()
    assert b'NEEDED' not in run('readelf','-d',exe)
    for i in range(N+2):
        r=subprocess.run([exe,str(i)],capture_output=True,timeout=10)
        assert r.returncode==expected(i) and not r.stdout and not r.stderr,(major,i,expected(i),r)
    r=subprocess.run([exe,'write'],capture_output=True,timeout=10)
    assert r.returncode==0 and not r.stdout and not r.stderr,(major,r)
    print(f'LLVM {major}: unmodified SQLite 3.54.1 cold read-only memdb, all {N+2} rowids vs independent predicate, immutable image, static libc closure PASS',flush=True)
