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

DOS/DOSBox-X gotchas this relay works around:
  - COMMAND.COM does NOT support `2>&1` (creates literal file named &1).
    Only stdout is captured. stderr is lost.
  - `CALL batch > file` does NOT capture output from commands inside
    the called batch — only the CALL builtin itself. We embed per-command
    `>> _RESULT.TXT` redirects in _DOSCMD.BAT instead.
  - CR+LF required for all batch files. LF-only parses but fails silently
    on GOTO labels and IF statements. copy_relay_bat() normalizes on write.
  - Absolute paths (C:\\_DONE.TXT) for all sentinel files, because commands
    inside _DOSCMD.BAT may CD to subdirectories.
  - DOSBox-X launched with -nopromptfolder to skip the first-run dialog.
  - CP437 encoding (not ASCII) for batch file content, to preserve DOS
    character set characters in arguments.
  - Return codes are quantized (30 thresholds, 1-10 exact then steps of
    5/10/25) because COMMAND.COM lacks %ERRORLEVEL% — we use IF ERRORLEVEL
    cascade.

See also: feedback_dosbox_automation.md for the complete 10-rule reference.
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
    """Locate dosbox-x executable.

    F-20: Checks (in order) the $DOSBOX_X env override, PATH via shutil.which,
    WinGet's version-stamped install path (joncampbell123.DOSBox-X_*), then
    standard Program Files / LocalAppData install locations.
    """
    # $DOSBOX_X env override takes highest priority
    env_override = os.environ.get("DOSBOX_X")
    if env_override and Path(env_override).is_file():
        return env_override

    # Check PATH
    exe = shutil.which("dosbox-x")
    if exe:
        return exe

    # Check common install locations on Windows
    candidates = [
        Path(os.environ.get("PROGRAMFILES", "")) / "DOSBox-X" / "dosbox-x.exe",
        Path(os.environ.get("PROGRAMFILES(X86)", "")) / "DOSBox-X" / "dosbox-x.exe",
        Path(os.environ.get("LOCALAPPDATA", "")) / "DOSBox-X" / "dosbox-x.exe",
    ]

    # F-20: WinGet install path (version-stamped; glob across versions)
    winget_base = Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "WinGet" / "Packages"
    if winget_base.is_dir():
        for pkg in winget_base.glob("joncampbell123.DOSBox-X_*"):
            exe_path = pkg / "bin" / "x64" / "Release SDL2" / "dosbox-x.exe"
            if exe_path.is_file():
                candidates.append(exe_path)

    for p in candidates:
        if p.is_file():
            return str(p)

    raise FileNotFoundError(
        "dosbox-x not found. Install DOSBox-X and ensure it is on PATH, "
        "set $DOSBOX_X, or install via WinGet (joncampbell123.DOSBox-X)."
    )


def _validate_mount_path(path) -> str:
    """F-04: Reject paths that would break DOSBox-X MOUNT parsing.

    Embedded quotes would terminate the quoted MOUNT argument early and
    allow injection of additional DOSBox-X commands. Trailing backslashes
    (except on drive roots like ``C:\\``) are stripped because they form
    ``\\"`` escape sequences that some parsers choke on.
    """
    s = str(path)
    if '"' in s:
        raise ValueError(f"Mount path contains quote: {s!r}")
    # Strip trailing backslashes except for drive roots like C:\
    while s.endswith("\\") and not (len(s) == 3 and s[1] == ":"):
        s = s.rstrip("\\")
    if not s:
        raise ValueError(f"Mount path is empty after normalization: {path!r}")
    return s


def generate_conf(work_dir: Path,
                  extra_mounts: list[tuple[str, str]] | None = None) -> Path:
    """Generate a temporary DOSBox-X config based on dev.conf.

    Rewrites the [autoexec] section to mount work_dir as C: and
    optionally add extra mount points.
    """
    conf_template = DEVENV_DIR / "dev.conf"
    if not conf_template.is_file():
        raise FileNotFoundError(f"Missing config template: {conf_template}")

    # F-04: validate mount paths before interpolation
    safe_work_dir = _validate_mount_path(work_dir)

    # Read template up to [autoexec]
    # F-09: track whether [autoexec] was actually seen so we can add one
    # if the user (or a future template edit) removes it. Without this,
    # generate_conf would silently emit a config missing the autoexec
    # section and dosrun would hang until timeout with no diagnostic.
    lines = conf_template.read_text(encoding="utf-8").splitlines()
    config_lines = []
    seen_autoexec = False
    for line in lines:
        if line.strip().lower() == "[autoexec]":
            config_lines.append(line)
            seen_autoexec = True
            break
        config_lines.append(line)

    if not seen_autoexec:
        # Template is malformed or user edited out [autoexec]; add it
        config_lines.append("[autoexec]")

    # Build autoexec section
    config_lines.append("@ECHO OFF")
    config_lines.append(f'MOUNT C "{safe_work_dir}"')

    if extra_mounts:
        for drive, host_path in extra_mounts:
            if not re.match(r'^[A-Za-z]:$', drive):
                raise ValueError(f"Invalid drive letter: {drive!r} (expected e.g. 'D:')")
            if drive.upper() == "C:":
                raise ValueError("Cannot use C: as extra mount (reserved for work dir)")
            safe_host = _validate_mount_path(host_path)
            config_lines.append(f'MOUNT {drive} "{safe_host}"')

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

    F4 caveat: the per-command `>> _RESULT.TXT` append redirect can
    clobber ERRORLEVEL on some COMMAND.COM versions -- if the redirect
    itself succeeds, ERRORLEVEL may be reset to 0 even when the
    command failed. The return code written to _RETCODE.TXT therefore
    reflects the LAST statement's exit status, which may be the
    redirect's success rather than the user command's failure. There
    is no clean fix in DOS batch: COMMAND.COM has no %ERRORLEVEL%
    variable to stash between the command and the redirect. For
    reliable exit-status checking, run one command per dosrun call.
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
    # F-03: validate CP437 strictly. errors="replace" silently substitutes
    # '?' for non-encodable characters, which can corrupt filenames or
    # command arguments without any diagnostic. Strict encoding with a
    # helpful error message is better than a mystery failure in DOS.
    try:
        encoded = content.encode("cp437", errors="strict")
    except UnicodeEncodeError as e:
        bad_char = content[e.start:e.end]
        raise ValueError(
            f"Command contains characters outside CP437 at position "
            f"{e.start}: {bad_char!r}. DOS cannot represent these."
        )
    cmd_path.write_bytes(encoded)


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
    # F-03: strict CP437 -- silent replacement would corrupt the relay
    try:
        encoded = text.encode("cp437", errors="strict")
    except UnicodeEncodeError as e:
        bad_char = text[e.start:e.end]
        raise ValueError(
            f"_RELAY.BAT contains characters outside CP437 at position "
            f"{e.start}: {bad_char!r}. DOS cannot represent these."
        )
    try:
        if os.path.samefile(str(src), str(dst)):
            return  # Don't rewrite the source file
    except (OSError, ValueError):
        pass  # dst doesn't exist yet, normal case
    dst.write_bytes(encoded)


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

    # Acquire lockfile to prevent concurrent invocations on same work_dir.
    #
    # F2 fix: atomically create-AND-write. The old pattern was
    #   open(O_CREAT|O_EXCL)  # success == lock acquired
    #   ... finally: write(pid+ts)
    # A Ctrl+C (or any exception) between open and write left a lock
    # file with no content, which then looked "unreadable" to every
    # subsequent run. _try_acquire_lock writes the payload BEFORE
    # returning success, so the lock file is either absent or complete.
    lock_path = work_dir / LOCK_FILE

    def _try_acquire_lock(path: Path, payload: str) -> bool:
        """Atomically create lock file with payload.

        Returns True on success, False if the lock file already exists.
        On any error after the O_EXCL open succeeds, cleans up the
        partial lock file before re-raising.
        """
        try:
            fd = os.open(str(path), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except OSError:
            return False
        try:
            os.write(fd, payload.encode("ascii"))
            os.close(fd)
            return True
        except OSError:
            try:
                os.close(fd)
            except OSError:
                pass
            try:
                os.unlink(str(path))
            except OSError:
                pass
            raise

    # F-07: include this invocation's timeout in the payload so a later
    # dosrun checking staleness can use the victim's own timeout (a long
    # --timeout 600 run should not be treated as stale after 90s just
    # because DEFAULT_TIMEOUT*3 == 90).
    payload = f"{os.getpid()}\n{time.time()}\n{timeout}"
    if not _try_acquire_lock(lock_path, payload):
        # Lock file exists -- check if the owning process is still alive.
        pid_text = "<unreadable>"
        old_pid = -1
        lock_is_stale = False
        try:
            lock_content = lock_path.read_text(
                encoding="ascii", errors="ignore"
            ).strip()
            lock_lines = lock_content.split("\n")
            pid_text = lock_lines[0].strip()
            old_pid = int(pid_text)
            lock_time = float(lock_lines[1].strip()) if len(lock_lines) >= 2 else 0.0
            # F-07: read the victim's own timeout (3rd line). Old-format
            # lockfiles (2 lines) fall back to DEFAULT_TIMEOUT.
            try:
                victim_timeout = int(lock_lines[2].strip()) if len(lock_lines) >= 3 else DEFAULT_TIMEOUT
            except ValueError:
                victim_timeout = DEFAULT_TIMEOUT
            lock_age = time.time() - lock_time

            # F3 fix: check timestamp FIRST. On Windows, PID recycling
            # means os.kill(pid, 0) can report "alive" for a PID that
            # was reassigned to an unrelated process after the original
            # dosrun died. A lock older than 3x the victim's own timeout
            # cannot possibly be from a legitimately-running dosrun,
            # so treat it as stale regardless of os.kill's answer.
            if lock_age > victim_timeout * 3:
                lock_is_stale = True
            else:
                # Recent lock -- use os.kill as a fast-path for
                # definitely-dead detection. If it succeeds we
                # conservatively assume the PID belongs to the original
                # dosrun (it might be recycled, but we'd rather wait
                # than stomp on a live peer).
                try:
                    os.kill(old_pid, 0)
                    lock_is_stale = False
                except (OSError, ProcessLookupError):
                    lock_is_stale = True
        except (ValueError, OSError):
            # PID/timestamp unreadable -- treat as stale.
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
            if not _try_acquire_lock(lock_path, payload):
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

    conf_path = None
    stderr_file = None
    stderr_tmp = None
    # F-08: define proc before try so it's in scope in finally. Without
    # this, an exception before Popen leaves proc undefined and the
    # reap path itself NameErrors, masking the original error.
    proc = None

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
        # F-08: ensure DOSBox-X is reaped even if the timeout-path
        # proc.kill/wait raised (e.g., the OS couldn't kill the handle).
        # Without this, a crashed kill path leaks a DOSBox-X process
        # AND leaves the lockfile holding future dosruns hostage.
        if proc is not None:
            try:
                if proc.poll() is None:
                    proc.kill()
                    try:
                        proc.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        pass  # last-resort: OS will reap
            except (OSError, AttributeError):
                pass
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
        # Remove copied _RELAY.BAT from work dir.
        # F12 fix: if work_dir happens to be devenv/ itself (e.g. the
        # user passed --work-dir devenv), the "copied" relay IS the
        # source file. Deleting it would destroy the authoritative
        # copy.
        # F-11: use os.path.samefile rather than Path.resolve() equality.
        # resolve() can differ between the two paths when junctions,
        # case-insensitive filesystems, or 8.3 short names are involved;
        # samefile compares inode/file identity and correctly identifies
        # two paths pointing at the same on-disk file.
        try:
            is_devenv = os.path.samefile(str(work_dir), str(DEVENV_DIR))
        except (OSError, ValueError):
            is_devenv = False
        relay_in_workdir = work_dir / RELAY_BAT
        if relay_in_workdir.is_file() and not is_devenv:
            try:
                relay_in_workdir.unlink(missing_ok=True)
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
