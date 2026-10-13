"""Compile and run the real firmware with fake UART/ADC/NVS/time (no board)."""
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
zig = root / ".pio/host-tools/ziglang/zig.exe"
compiler = shutil.which("g++") or shutil.which("clang++")
if compiler:
    command = [compiler]
elif zig.exists():
    command = [str(zig), "c++"]
else:
    raise SystemExit("Install a host C++ compiler, or: python -m pip install --target .pio/host-tools ziglang==0.13.0")
output = root / ".pio/host-tests/firmware-tests.exe"
output.parent.mkdir(parents=True, exist_ok=True)
command += ["-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", "-Itest/native/stubs", "-Isrc"]
command += [str(path.relative_to(root)) for path in sorted((root / "src").rglob("*.cpp"))]
command += ["test/native/test_firmware.cpp", "-o", str(output)]
subprocess.run(command, cwd=root, check=True)
raise SystemExit(subprocess.run([str(output)], cwd=root).returncode)
