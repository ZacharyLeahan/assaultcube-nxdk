"""Compile and run the portable bot-match regression with host Clang or GCC."""
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
cc, cxx = os.getenv("CC", "cc"), os.getenv("CXX", "c++")


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


with TemporaryDirectory() as directory:
    tmp = Path(directory)
    # Use the host ABI's label prefix with the same baked assets as the XBE.
    asm = tmp / "assets.S"
    text = ["#define JOIN_(a,b) a##b", "#define JOIN(a,b) JOIN_(a,b)",
            "#define NAME(n) JOIN(__USER_LABEL_PREFIX__,n)", ".data"]
    for name in ("world","deltas","ambience","gpu_vertices"):
        text += [".balign 16", f".globl NAME({name})", f"NAME({name}):",
                 f'.incbin "generated/{name}.bin"']
    asm.write_text("\n".join(text) + "\n")
    objects = []
    for source in (asm, Path("navigation.c"), Path("prop_depth.c"), Path("ambience.c"), Path("match.cpp"), Path("tests/duel.c")):
        obj = tmp / (source.stem + ".o")
        run(cxx if source.suffix == ".cpp" else cc, "-O2", "-c", source, "-o", obj)
        objects.append(obj)
    exe = tmp / ("duel.exe" if os.name == "nt" else "duel")
    run(cxx, *objects, "-o", exe)
    run(exe)
