"""编译实际菜单运动函数的宿主测试；不需要连接或烧写设备。"""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
candidates = (os.environ.get("CXX"), shutil.which("g++"), r"C:\msys64\mingw64\bin\g++.exe")
compiler = next((Path(p) for p in candidates if p and Path(p).is_file()), None)
if compiler is None:
    raise SystemExit("未找到 g++，无法运行菜单运动宿主测试")
environment = os.environ.copy()
environment["PATH"] = str(compiler.parent) + os.pathsep + environment.get("PATH", "")
build = root / ".pio" / "host_tools"
build.mkdir(parents=True, exist_ok=True)
executable = build / "menu_motion_tests.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "src/ui/includes"),
                str(root / "tests/ui/test_menu_motion.cpp"), "-o", str(executable)],
               cwd=root, env=environment, check=True)
subprocess.run([str(executable)], cwd=root, env=environment, check=True)
