"""Patch NimBLE's default implicit bond deletion before compiling the firmware."""

from pathlib import Path
import sys

Import("env")

sys.dont_write_bytecode = True
from nimble_pairing_policy import apply_repeat_pairing_policy


library_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
source_path = library_dir / "NimBLE-Arduino" / "src" / "NimBLEServer.cpp"
if not source_path.is_file():
    raise FileNotFoundError(f"NimBLE server source not found: {source_path}")

source = source_path.read_text(encoding="utf-8")
patched = apply_repeat_pairing_policy(source)
if patched != source:
    source_path.write_text(patched, encoding="utf-8")
    print("[蓝牙] NimBLE 重复配对策略已设为仅允许显式配对。")
