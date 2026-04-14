#!/usr/bin/env python3
"""dosrun.py - Run DOS commands inside DOSBox-X and capture output.

Usage:
    python dosrun.py "VER"
    python dosrun.py "DIR C:\\" "ECHO Hello"
    python dosrun.py --timeout 10 "NISATEST.COM"
    python dosrun.py --cwd \\phase0\\dos "NISATEST.COM"
    python dosrun.py --mount D: C:\\EXTRA "DIR D:\\"

The relay works by:
  1. Writing commands to _DOSCMD.BAT in the work directory
  2. Launching DOSBox-X with dev.conf (which mounts the project and
     calls _RELAY.BAT from autoexec)
  3. _RELAY.BAT executes _DOSCMD.BAT, captures stdout to _RESULT.TXT,
     writes exit code to _RETCODE.TXT, and signals via _DONE.TXT
  4. This script waits for _DONE.TXT, reads the results, cleans up
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

DEVENV_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = DEVENV_DIR.parent
DEFAULT_TIMEOUT = 30  # seconds

# Temp file names (on the DOS side, in the mounted work dir)
RELAY_BAT = "_RELAY.BAT"
DOSCMD_BAT = "_DOSCMD.BAT"
RESULT_TXT = "_RESULT.TXT"
RETCODE_TXT = "_RETCODE.TXT"
DONE_TXT = "_DONE.TXT"

LOCK_FILE = "_DOSRUN.LCK"

TEMP_FILES = [DOSCMD_BAT, RESULT_TXT, RETCODE_TXT, DONE_TXT]


def find_dosbox_x() -> str:
    """Locate dosbox-x executable."""
    # Check PATH first
    exe = shutil.which("dosbox-x")
    if exe:
        return exe

    # Check common install locations on Windows
    candidates = [
        Path(os.environ.get("PROGRAMFILES", "")) / "DOSBox-X" / "dosbox-x.exe",
        Path(os.environ.get("PROGRAMFILES(X86)", "")) / "DOSBox-X" / "dosbox-x.exe",
        Path(os.environ.get("LOCALAPPDATA", "")) / "DOSBox-X" / "dosbox-x.exe",
    ]
    for p in candidates:
        if p.is_file():
            return str(p)

    raise FileNotFoundError(
        "dosbox-x not found. Install DOSBox-X and ensure it is on PATH "
        "or in a standard install location."
    )


def generate_conf(work_dir: Path,
                  extra_mounts: list[tuple[str, str]] | None = None) -> Path:
    """Generate a temporary DOSBox-X config based on dev.conf.

    Rewrites the [autoexec] section to mount work_dir as C: and
    optionally add extra mount points.
    """
    conf_template = DEVENV_DIR / "dev.conf"
    if not conf_template.is_file():
        raise FileNotFoundError(f"Missing config template: {conf_template}")

    # Read template up to [autoexec]
    lines = conf_template.read_text(encoding="utf-8").splitlines()
    config_lines = []
    for line in lines:
        if line.strip().lower() == "[autoexec]":
            config_lines.append(line)
            break
        config_lines.append(line)

    # Build autoexec section
    config_lines.append("@ECHO OFF")
    config_lines.append(f'MOUNT C "{work_dir}"')

    if extra_mounts:
        for drive, host_path in extra_mounts:
            if not re.match(r'^[A-Za-z]:$', drive):
                raise ValueError(f"Invalid drive letter: {drive!r} (expected e.g. 'D:')")
            if drive.upper() == "C:":
                raise ValueError("Cannot use C: as extra mount (reserved for work dir)")
            config_lines.append(f'MOUNT {drive} "{host_path}"')

    config_lines.append("C:")
    config_lines.append("IF EXIST C:\\_RELAY.BAT CALL C:\\_RELAY.BAT")
    config_lines.append("EXIT")

    # Write to temp file
    fd, conf_path = tempfile.mkstemp(suffix=".conf", prefix="dosrun_")
    os.close(fd)
    Path(conf_path).write_text("\r\n".join(config_lines), encoding="utf-8")
    return Path(conf_path)


def write_doscmd(work_dir: Path, commands: list[str],
                 cwd: str | None = None) -> None:
    """Write the DOS command batch file with CR+LF line endings.

    Each command's output is appended to _RESULT.TXT from within the
    batch itself, avoiding DOSBox-X COMMAND.COM limitations with
    batch-level output redirection.
    """
    cmd_path = work_dir / DOSCMD_BAT
    result_path = "C:\\" + RESULT_TXT
    content = "@ECHO OFF\r\n"
    # Initialize result file
    content += f"TYPE NUL > {result_path}\r\n"
    if cwd:
        dos_cwd = cwd.replace("/", "\\")
        if not re.match(r'^[A-Za-z0-9\\:_.\- ]+$', dos_cwd):
            raise ValueError(f"Invalid DOS path characters in cwd: {dos_cwd!r}")
        content += f"CD {dos_cwd}\r\n"
    for cmd in commands:
        # Strip CR/LF to prevent command injection via embedded newlines
        sanitized = cmd.replace("\r", "").replace("\n", "")
        content += f"{sanitized} >> {result_path}\r\n"
    cmd_path.write_bytes(content.encode("cp437", errors="replace"))


def cleanup_temp_files(work_dir: Path) -> None:
    """Remove relay temp files from the work directory."""
    for name in TEMP_FILES:
        p = work_dir / name
        try:
            p.unlink(missing_ok=True)
        except OSError:
            pass


def copy_relay_bat(work_dir: Path) -> None:
    """Copy _RELAY.BAT to the work directory, ensuring CR+LF line endings."""
    src = DEVENV_DIR / RELAY_BAT
    dst = work_dir / RELAY_BAT
    if not src.is_file():
        raise FileNotFoundError(f"Missing relay script: {src}")
    # Read and normalize to CRLF for DOS compatibility
    text = src.read_text(encoding="utf-8")
    text = text.replace("\r\n", "\n").replace("\n", "\r\n")
    dst.write_bytes(text.encode("cp437", errors="replace"))


def run_dos(commands: list[str], timeout: int = DEFAULT_TIMEOUT,
            work_dir: Path | None = None, cwd: str | None = None,
            extra_mounts: list[tuple[str, str]] | None = None,
            verbose: bool = False) -> tuple[str, int]:
    """Execute DOS commands in DOSBox-X and return (output, retcode).

    Args:
        commands: List of DOS commands to execute
        timeout: Maximum seconds to wait for DOSBox-X
        work_dir: Host directory to mount as C: (default: PROJECT_ROOT)
        cwd: DOS-side working directory (e.g. \\phase0\\dos)
        extra_mounts: List of (drive_letter, host_path) tuples
        verbose: Print debug info

    Returns:
        Tuple of (captured stdout text, DOS errorlevel)

    Raises:
        TimeoutError: If DOSBox-X doesn't finish within timeout
        FileNotFoundError: If dosbox-x or required files missing
        RuntimeError: If relay didn't produce expected output files
    """
    if work_dir is None:
        work_dir = PROJECT_ROOT

    work_dir = Path(work_dir).resolve()
    if not work_dir.is_dir():
        raise FileNotFoundError(f"Work directory not found: {work_dir}")

    dosbox_exe = find_dosbox_x()
    if verbose:
        print(f"[dosrun] DOSBox-X: {dosbox_exe}", file=sys.stderr)
        print(f"[dosrun] Work dir: {work_dir}", file=sys.stderr)
        print(f"[dosrun] Commands: {commands}", file=sys.stderr)

    # Acquire lockfile to prevent concurrent invocations on same work_dir
    lock_path = work_dir / LOCK_FILE
    lock_fd = None
    try:
        lock_fd = os.open(str(lock_path), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except OSError:
        # Lock file exists -- check if the owning process is still alive
        pid_text = "<unreadable>"
        lock_is_stale = False
        try:
            lock_content = lock_path.read_text(
                encoding="ascii", errors="ignore"
            ).strip()
            lock_lines = lock_content.split("\n")
            pid_text = lock_lines[0].strip()
            old_pid = int(pid_text)
            # Check timestamp-based staleness (defeats PID recycling)
            if len(lock_lines) >= 2:
                lock_time = float(lock_lines[1].strip())
                age = time.time() - lock_time
                if age > DEFAULT_TIMEOUT * 3:
                    lock_is_stale = True
            if not lock_is_stale:
                os.kill(old_pid, 0)  # signal 0 = existence check
        except (ValueError, OSError):
            # Process is dead or PID/timestamp unreadable -- stale lock
            lock_is_stale = True

        if lock_is_stale:
            print(
                f"[dosrun] WARNING: Removing stale lock file (PID {pid_text!r})",
                file=sys.stderr,
            )
            # NOTE: TOCTOU race between unlink and re-create is a known
            # limitation. For a single-developer tool the window is
            # negligible; adding flock/LockFileEx is not worth the
            # cross-platform complexity.
            try:
                lock_path.unlink()
            except OSError:
                pass
            # Retry atomic create
            try:
                lock_fd = os.open(str(lock_path), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            except OSError:
                raise RuntimeError(
                    f"Another dosrun invocation is using {work_dir} "
                    f"(lockfile {lock_path} exists)."
                )
        else:
            # Process is still alive
            raise RuntimeError(
                f"Another dosrun invocation (PID {old_pid}) is using {work_dir} "
                f"(lockfile {lock_path} exists). Wait or remove it manually."
            )
    finally:
        if lock_fd is not None:
            lock_data = f"{os.getpid()}\n{time.time()}"
            os.write(lock_fd, lock_data.encode("ascii"))
            os.close(lock_fd)

    conf_path = None
    stderr_file = None
    stderr_tmp = None

    try:
        # Clean up any stale temp files from previous runs
        cleanup_temp_files(work_dir)

        done_path = work_dir / DONE_TXT
        result_path = work_dir / RESULT_TXT
        retcode_path = work_dir / RETCODE_TXT
        # Copy relay script and write command batch
        copy_relay_bat(work_dir)
        write_doscmd(work_dir, commands, cwd=cwd)

        # Generate config
        conf_path = generate_conf(work_dir, extra_mounts=extra_mounts)
        # Launch DOSBox-X (capture stderr to a temp file to avoid pipe
        # deadlock if DOSBox-X produces more than the OS pipe buffer)
        stderr_fd, stderr_tmp = tempfile.mkstemp(
            suffix=".stderr", prefix="dosrun_"
        )
        stderr_file = os.fdopen(stderr_fd, "w")
        proc = subprocess.Popen(
            [dosbox_exe, "-conf", str(conf_path), "-nopromptfolder"],
            stdout=subprocess.DEVNULL,
            stderr=stderr_file,
        )

        if verbose:
            print(f"[dosrun] PID: {proc.pid}", file=sys.stderr)

        # Wait for _DONE.TXT sentinel
        start = time.monotonic()
        while not done_path.is_file():
            elapsed = time.monotonic() - start
            if elapsed > timeout:
                proc.kill()
                proc.wait(timeout=5)
                stderr_file.close()
                dosbox_stderr = Path(stderr_tmp).read_text(
                    encoding="utf-8", errors="replace"
                ).strip()
                msg = (
                    f"DOSBox-X did not finish within {timeout}s. "
                    f"Process killed (PID {proc.pid})."
                )
                if dosbox_stderr:
                    msg += f"\nDOSBox-X stderr:\n{dosbox_stderr}"
                raise TimeoutError(msg)
            # Check if process died unexpectedly
            if proc.poll() is not None and not done_path.is_file():
                # Process exited but no done file -- give a brief grace
                # period for filesystem flush
                time.sleep(0.5)
                if not done_path.is_file():
                    stderr_file.close()
                    dosbox_stderr = Path(stderr_tmp).read_text(
                        encoding="utf-8", errors="replace"
                    ).strip()
                    msg = (
                        f"DOSBox-X exited unexpectedly (code {proc.returncode}) "
                        f"without producing {DONE_TXT}"
                    )
                    if dosbox_stderr:
                        msg += f"\nDOSBox-X stderr:\n{dosbox_stderr}"
                    raise RuntimeError(msg)
            time.sleep(0.25)

        # Wait for DOSBox-X to fully exit
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

        # Print DOSBox-X stderr in verbose mode
        stderr_file.close()
        dosbox_stderr = Path(stderr_tmp).read_text(
            encoding="utf-8", errors="replace"
        ).strip()
        if verbose and dosbox_stderr:
            print(
                f"[dosrun] DOSBox-X stderr:\n{dosbox_stderr}",
                file=sys.stderr,
            )

        # Check for NO_CMD sentinel (relay found no command file)
        done_content = done_path.read_text(
            encoding="ascii", errors="ignore"
        ).strip()
        if done_content == "NO_CMD":
            raise RuntimeError(
                "Relay found no command file (_DOSCMD.BAT missing)"
            )

        # Read results
        output = ""
        if result_path.is_file():
            raw = result_path.read_bytes()
            # DOS output is CP437, decode accordingly
            output = raw.decode("cp437", errors="replace")
            # Normalize line endings
            output = output.replace("\r\n", "\n").strip()

        # Retry loop for _RETCODE.TXT (may lag behind _DONE.TXT flush)
        retcode = None  # sentinel: not yet read
        for _attempt in range(5):
            if retcode_path.is_file():
                rc_text = retcode_path.read_text(
                    encoding="ascii", errors="ignore"
                ).strip()
                if rc_text:
                    try:
                        retcode = int(rc_text)
                    except ValueError:
                        pass
                    break
            time.sleep(0.1)

        if retcode is None:
            print(
                "[dosrun] WARNING: _RETCODE.TXT missing or empty after "
                "retry exhaustion; assuming failure (255)",
                file=sys.stderr,
            )
            retcode = 255

        return output, retcode

    finally:
        # Clean up stderr temp file
        if stderr_tmp is not None:
            try:
                if stderr_file is not None and not stderr_file.closed:
                    stderr_file.close()
                os.unlink(stderr_tmp)
            except OSError:
                pass
        # Clean up
        if conf_path is not None:
            try:
                conf_path.unlink(missing_ok=True)
            except OSError:
                pass
        cleanup_temp_files(work_dir)
        # Remove copied _RELAY.BAT from work dir (but not from devenv/)
        try:
            (work_dir / RELAY_BAT).unlink(missing_ok=True)
        except OSError:
            pass
        # Release lockfile
        try:
            lock_path.unlink(missing_ok=True)
        except OSError:
            pass


def main():
    parser = argparse.ArgumentParser(
        description="Run DOS commands in DOSBox-X and capture output"
    )
    parser.add_argument(
        "commands", nargs="+",
        help="DOS commands to execute (each argument is one command line)"
    )
    parser.add_argument(
        "--timeout", type=int, default=DEFAULT_TIMEOUT,
        help=f"Timeout in seconds (default: {DEFAULT_TIMEOUT})"
    )
    parser.add_argument(
        "--cwd", type=str, default=None,
        help="DOS-side working directory (e.g. \\\\phase0\\\\dos)"
    )
    parser.add_argument(
        "--work-dir", type=str, default=None,
        help="Host directory to mount as C: (default: project root)"
    )
    parser.add_argument(
        "--mount", nargs=2, action="append", metavar=("DRIVE", "PATH"),
        help="Additional mount point (e.g. --mount D: C:\\\\EXTRA)"
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="Print debug information"
    )

    args = parser.parse_args()

    work_dir = Path(args.work_dir) if args.work_dir else None

    try:
        output, retcode = run_dos(
            commands=args.commands,
            timeout=args.timeout,
            work_dir=work_dir,
            cwd=args.cwd,
            extra_mounts=args.mount,
            verbose=args.verbose,
        )
        if output:
            print(output)
        sys.exit(retcode)
    except TimeoutError as e:
        print(f"TIMEOUT: {e}", file=sys.stderr)
        sys.exit(124)
    except FileNotFoundError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    except RuntimeError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
