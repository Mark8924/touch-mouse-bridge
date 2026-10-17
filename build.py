#!/usr/bin/env python3
"""Build standalone Windows binaries using llvm-mingw (Python standard library only)."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path,
                        help="llvm-mingw root or bin directory; alternatively set LLVM_MINGW_BIN")
    parser.add_argument("--arch", choices=("arm64", "x64", "all"), default="all")
    parser.add_argument("--output", type=Path, help="output directory (default: dist)")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    output = (args.output or root / "dist").resolve()
    toolchain = args.toolchain or os.environ.get("LLVM_MINGW_BIN")
    tool_dir = Path(toolchain).expanduser().resolve() if toolchain else None
    if tool_dir and (tool_dir / "bin").is_dir():
        tool_dir /= "bin"

    def tool(name):
        if tool_dir:
            for suffix in ("", ".exe"):
                candidate = tool_dir / (name + suffix)
                if candidate.is_file():
                    return str(candidate)
        else:
            found = shutil.which(name)
            if found:
                return found
        parser.error(f"Cannot find {name}. Use --toolchain or add llvm-mingw/bin to PATH.")

    targets = [("arm64", "aarch64"), ("x64", "x86_64")]
    targets = [(a, t, tool(f"{t}-w64-mingw32-windres"),
                tool(f"{t}-w64-mingw32-clang++"))
               for a, t in targets if args.arch in (a, "all")]
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="touchmouse-build-") as tmp:
        for arch, _, windres, compiler in targets:
            resource = str(Path(tmp) / f"app-{arch}.o")
            subprocess.run([windres, "-i", "app.rc", "-o", resource], cwd=root, check=True)
            executable = output / f"TouchMouse-{arch}.exe"
            subprocess.run([
                compiler, "-std=c++17", "-O2", "-static", "-municode", "-mwindows",
                "-Wall", "-Wextra", "-Werror", "TouchMouse.cpp", resource,
                "-o", str(executable), "-lcomctl32", "-lcomdlg32", "-lshell32", "-lole32",
                "-ldwmapi", "-lwtsapi32", "-ladvapi32", "-lgdi32", "-luser32",
            ], cwd=root, check=True)
            print(f"{arch}: {executable} ({executable.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
