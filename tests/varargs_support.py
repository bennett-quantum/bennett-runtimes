"""Prepare the published N23 x86-64 va-block layout for component fixtures."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import llvm_dir
ROOT=Path(__file__).resolve().parents[1]
def materialise(major,source,output):
    source,output=Path(source),Path(output)
    assert source.resolve()!=output.resolve()
    before=hashlib.sha256(source.read_bytes()).hexdigest()
    cpp=ROOT/'tests/materialise_varargs.cpp'
    key=hashlib.sha256(cpp.read_bytes()).hexdigest()[:20]
    cache=Path(os.environ['TMPDIR'])/'varargs-fixture'; cache.mkdir(parents=True,exist_ok=True)
    exe=cache/f'n23-{major}-{key}'
    llvm=llvm_dir(major)
    if not exe.exists():
        flags=subprocess.check_output([llvm/'llvm-config','--cxxflags','--ldflags','--system-libs','--libs','core','irreader','bitwriter'],text=True)
        subprocess.run([llvm/'clang++',*shlex.split(flags),'-std=c++17','-O2',cpp,'-o',exe],check=True)
    subprocess.run([exe,source,output],check=True)
    assert hashlib.sha256(source.read_bytes()).hexdigest()==before
