#!/usr/bin/env python3
"""dosbuild.py - Native compile on Windows, then run in DOSBox-X.

Compiles source files natively using OpenWatcom (C) or NASM (assembly)
on the Windows host (fast), then optionally runs the resulting executable
inside DOSBox-X via the dosrun relay.

Usage:
    # Assemble and run a .COM file
    python dosbuild.py --asm phase0\\dos\\nisatest.asm --run NISATEST.COM

    # Compile C to .OBJ, link to .EXE, run
    python dosbuild.py --cc dos\\lib\\screen.c --link SCREEN.EXE --run SCREEN.EXE

    # Just compile, don't run
    python dosbuild.py --asm phase0\\dos\\nisatest.asm

    # Compile with custom flags
    python dosbuild.py --cc foo.c --cc-flags "-0 -ms -s -ox -w4 -zq -bt=dos"
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

# Add devenv to path for dosrun import
DEVENV_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DEVENV_DIR))
from dosrun import run_dos, PROJECT_ROOT

# OpenWatcom paths
WATCOM = Path(os.environ.get("WATCOM", r"C:\WATCOM"))

WCC = None
WLINK = None
NASM = None
_tools_resolved = False


def _resolve_tools():
    global WCC, WLINK, NASM, _tools_resolved
    if _tools_resolved:
        return
    _tools_resolved = True
    # Prefer 64-bit tools on 64-bit Windows
    WCC = WATCOM / "BINNT64" / "wcc.exe"
    if not WCC.is_file():
        WCC = WATCOM / "BINNT" / "wcc.exe"
    WLINK = WATCOM / "BINNT64" / "wlink.exe"
    if not WLINK.is_file():
        WLINK = WATCOM / "BINNT" / "wlink.exe"
    NASM = shutil.which("nasm")
    if not NASM:
        _nasm_prog = Path(os.environ.get("PROGRAMFILES", "")) / "NASM" / "nasm.exe"
        if _nasm_prog.is_file():
            NASM = str(_nasm_prog)

# Default compiler flags for 8088 real-mode DOS
DEFAULT_CC_FLAGS = "-0 -ms -s -ox -w4 -zq -bt=dos"
DEFAULT_NASM_FLAGS = "-f bin"


def check_tools():
    """Verify required tools exist."""
    _resolve_tools()
    missing = []
    if not WCC.is_file():
        missing.append(f"OpenWatcom wcc not found at {WCC}")
    if not WLINK.is_file():
        missing.append(f"OpenWatcom wlink not found at {WLINK}")
    if not NASM:
        missing.append("NASM not found on PATH or in Program Files")
    return missing


def compile_asm(source: Path, output: Path | None = None,
                flags: str | None = None) -> Path:
    """Assemble a .ASM file to .COM using NASM."""
    _resolve_tools()
    if not NASM:
        raise FileNotFoundError("NASM not found")
    if not source.is_file():
        raise FileNotFoundError(f"Source not found: {source}")

    if output is None:
        output = source.with_suffix(".com")

    nasm_flags = flags if flags else DEFAULT_NASM_FLAGS
    cmd = [NASM] + nasm_flags.split() + ["-o", str(output), str(source)]

    print(f"[build] NASM: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"[build] NASM FAILED (exit {result.returncode}):", file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        raise RuntimeError(f"NASM assembly failed for {source}")

    if result.stderr:
        # NASM warnings
        print(f"[build] NASM warnings:\n{result.stderr}", file=sys.stderr)

    print(f"[build] Output: {output} ({output.stat().st_size} bytes)")
    return output


def compile_c(source: Path, output: Path | None = None,
              flags: str | None = None) -> Path:
    """Compile a .C file to .OBJ using OpenWatcom wcc."""
    _resolve_tools()
    if not WCC.is_file():
        raise FileNotFoundError(f"wcc not found at {WCC}")
    if not source.is_file():
        raise FileNotFoundError(f"Source not found: {source}")

    if output is None:
        output = source.with_suffix(".obj")

    cc_flags = flags if flags else DEFAULT_CC_FLAGS
    cmd = [str(WCC)] + cc_flags.split() + [
        f"-fo={output}",
        str(source),
    ]

    # OpenWatcom needs WATCOM and INCLUDE env vars
    env = os.environ.copy()
    env["WATCOM"] = str(WATCOM)
    env["INCLUDE"] = str(WATCOM / "H")
    env["PATH"] = str(WCC.parent) + os.pathsep + env.get("PATH", "")

    print(f"[build] WCC: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True, env=env)

    if result.returncode != 0:
        print(f"[build] WCC FAILED (exit {result.returncode}):", file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        raise RuntimeError(f"WCC compilation failed for {source}")

    if result.stdout.strip():
        print(f"[build] WCC output: {result.stdout.strip()}")

    print(f"[build] Output: {output} ({output.stat().st_size} bytes)")
    return output


def link_exe(obj_files: list[Path], output: Path,
             system: str = "dos com") -> Path:
    """Link .OBJ files into a DOS executable using wlink.

    Uses a wlink directive file (@file) to avoid issues with multi-word
    arguments like ``system dos com`` being treated as single args.
    """
    _resolve_tools()
    if not WLINK.is_file():
        raise FileNotFoundError(f"wlink not found at {WLINK}")

    # Build wlink directive file content
    file_list = ", ".join(str(o) for o in obj_files)
    directives = "\n".join([
        f"system {system}",
        f"file {file_list}",
        f"name {output}",
        "option quiet",
    ])

    env = os.environ.copy()
    env["WATCOM"] = str(WATCOM)
    env["PATH"] = str(WLINK.parent) + os.pathsep + env.get("PATH", "")

    # Write directives to a temp file and invoke wlink with @file
    import tempfile as _tempfile
    fd, directive_path = _tempfile.mkstemp(suffix=".lnk", prefix="wlink_")
    try:
        os.write(fd, directives.encode("utf-8"))
        os.close(fd)
        fd = None

        cmd = [str(WLINK), f"@{directive_path}"]
        print(f"[build] WLINK: {' '.join(cmd)}")
        print(f"[build] Directives:\n{directives}")
        result = subprocess.run(cmd, capture_output=True, text=True, env=env)

        if result.returncode != 0:
            print(f"[build] WLINK FAILED (exit {result.returncode}):", file=sys.stderr)
            if result.stdout:
                print(result.stdout, file=sys.stderr)
            if result.stderr:
                print(result.stderr, file=sys.stderr)
            raise RuntimeError(f"Linking failed for {output}")

        print(f"[build] Linked: {output} ({output.stat().st_size} bytes)")
        return output
    finally:
        if fd is not None:
            os.close(fd)
        try:
            os.unlink(directive_path)
        except OSError:
            pass


def main():
    parser = argparse.ArgumentParser(
        description="Native compile + DOSBox-X test runner"
    )

    # Source inputs
    source_group = parser.add_argument_group("source files")
    source_group.add_argument(
        "--asm", type=str,
        help="NASM assembly source file (.asm)"
    )
    source_group.add_argument(
        "--cc", type=str, action="append",
        help="C source file(s) to compile (.c). Can be specified multiple times."
    )

    # Compiler options
    build_group = parser.add_argument_group("build options")
    build_group.add_argument(
        "--cc-flags", type=str, default=None,
        help=f'WCC compiler flags (default: "{DEFAULT_CC_FLAGS}")'
    )
    build_group.add_argument(
        "--asm-flags", type=str, default=None,
        help=f'NASM flags (default: "{DEFAULT_NASM_FLAGS}")'
    )
    build_group.add_argument(
        "--link", type=str, default=None,
        help="Link compiled objects into this executable name"
    )
    build_group.add_argument(
        "--link-system", type=str, default="dos com",
        help='wlink system target (default: "dos com")'
    )
    build_group.add_argument(
        "-o", "--output", type=str, default=None,
        help="Output file path (for single-source builds)"
    )

    # Run options
    run_group = parser.add_argument_group("run options")
    run_group.add_argument(
        "--run", type=str, default=None,
        help="Run this executable in DOSBox-X after building"
    )
    run_group.add_argument(
        "--run-args", type=str, default="",
        help="Arguments to pass to the DOS executable"
    )
    run_group.add_argument(
        "--cwd", type=str, default=None,
        help="DOS-side working directory for execution"
    )
    run_group.add_argument(
        "--timeout", type=int, default=30,
        help="DOSBox-X timeout in seconds (default: 30)"
    )
    run_group.add_argument(
        "--verbose", "-v", action="store_true",
        help="Print debug information"
    )

    args = parser.parse_args()

    if not args.asm and not args.cc:
        parser.error("Specify at least one source: --asm or --cc")

    # Check tools
    _resolve_tools()
    # Check only the tools we need for this build
    needed_errors = []
    if args.asm:
        if not NASM:
            needed_errors.append("NASM not found on PATH or in Program Files")
    if args.cc:
        if not WCC.is_file():
            needed_errors.append(f"OpenWatcom wcc not found at {WCC}")
        if not WLINK.is_file():
            needed_errors.append(f"OpenWatcom wlink not found at {WLINK}")
    if needed_errors:
        for err in needed_errors:
            print(f"ERROR: {err}", file=sys.stderr)
        sys.exit(1)

    built_objects = []

    try:
        # Assemble .ASM
        if args.asm:
            asm_path = Path(args.asm).resolve()
            out_path = Path(args.output).resolve() if args.output else None
            result = compile_asm(asm_path, output=out_path, flags=args.asm_flags)
            built_objects.append(result)

        # Compile .C files
        if args.cc:
            if args.output and len(args.cc) > 1:
                parser.error("--output cannot be used with multiple --cc files")
            for i, c_file in enumerate(args.cc):
                c_path = Path(c_file).resolve()
                out_path = Path(args.output).resolve() if (args.output and len(args.cc) == 1) else None
                obj = compile_c(c_path, output=out_path, flags=args.cc_flags)
                built_objects.append(obj)

            # Link if requested
            if args.link:
                link_output = Path(args.link).resolve()
                link_exe(built_objects, link_output, system=args.link_system)

        # Run in DOSBox-X if requested
        if args.run:
            run_cmd = args.run
            if args.run_args:
                run_cmd += " " + args.run_args

            print(f"\n[build] Running in DOSBox-X: {run_cmd}")

            output, retcode = run_dos(
                commands=[run_cmd],
                timeout=args.timeout,
                cwd=args.cwd,
                verbose=args.verbose,
            )

            print(f"\n--- DOSBox-X output ---")
            if output:
                print(output)
            print(f"--- Exit code: {retcode} ---")
            sys.exit(retcode)

        print("\n[build] Build complete.")

    except FileNotFoundError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    except RuntimeError as e:
        print(f"BUILD ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
