"""把本机环境变量中的绑定主密钥写入构建目录，不把秘密放进仓库或编译命令行。"""

import os
import re
from pathlib import Path

Import("env")


secret = os.environ.get("PRESCRIPT_DEVICE_BINDING_MASTER_SECRET", "").strip()
if not secret and os.name == "nt":
    # Codex/VS Code 可能早于用户环境变量启动；直接读取当前用户环境注册表可让后续构建立即生效，
    # 同时不在控制台输出秘密值。
    import winreg

    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as key:
            secret = str(
                winreg.QueryValueEx(key, "PRESCRIPT_DEVICE_BINDING_MASTER_SECRET")[0]
            ).strip()
    except OSError:
        secret = ""
if secret and (len(secret) < 32 or not re.fullmatch(r"[A-Za-z0-9_-]+", secret)):
    raise ValueError(
        "PRESCRIPT_DEVICE_BINDING_MASTER_SECRET must be at least 32 URL-safe characters"
    )

build_dir = Path(env.subst("$BUILD_DIR"))
build_dir.mkdir(parents=True, exist_ok=True)
header_path = build_dir / "generated_binding_secret.h"
content = (
    "#pragma once\n"
    f'#define PRESCRIPT_DEVICE_BINDING_MASTER_SECRET "{secret}"\n'
)
if not header_path.exists() or header_path.read_text(encoding="utf-8") != content:
    header_path.write_text(content, encoding="utf-8")

env.Append(CPPPATH=[str(build_dir)])
