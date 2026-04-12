#!/usr/bin/env python3
"""Build helper that strips MSYSTEM so ESP-IDF runs from MSYS2/Git Bash."""
import os, re, subprocess, sys

IDF_PATH = os.environ.get("IDF_PATH", r"C:\Espressif\frameworks\esp-idf-v5.5.4")
IDF_PYTHON = os.path.join(
    os.environ.get("IDF_PYTHON_ENV_PATH", r"C:\Espressif\python_env\idf5.5_py3.11_env"),
    "Scripts", "python.exe",
)

env = dict(os.environ)
env.pop("MSYSTEM", None)
env["IDF_PATH"] = IDF_PATH

# Get tool paths from idf_tools.py export
result = subprocess.run(
    [IDF_PYTHON, os.path.join(IDF_PATH, "tools", "idf_tools.py"), "export"],
    env=env, capture_output=True, text=True,
)

# Parse: export KEY="VALUE";export KEY2="VALUE2";...
for match in re.finditer(r'export\s+(\w+)="([^"]*)"', result.stdout):
    key, val = match.group(1), match.group(2)
    if key == "PATH":
        val = val.replace("%PATH%", env.get("PATH", ""))
    env[key] = val

# Run idf.py build, passing through any extra args
cmd = [IDF_PYTHON, os.path.join(IDF_PATH, "tools", "idf.py")] + (sys.argv[1:] or ["build"])
sys.exit(subprocess.run(cmd, env=env, cwd=os.path.dirname(os.path.abspath(__file__))).returncode)
