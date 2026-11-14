#!/usr/bin/env python3
"""Runs a command whose windows must not appear on the owner's screen.

    python scripts/background.py <command> [args...]

On Windows the command starts on its own desktop: its windows are created and drawn as usual,
but they never show on the visible desktop, take the focus or the keyboard. The game's own
screenshots and the debug harness keep working, since they read the rendered frame and talk
over a socket. Screen tools (ghosthand, the taskbar) can't see such a window. Elsewhere the
command simply runs.
"""

from __future__ import annotations

import subprocess
import sys

DESKTOP = "OneFourSevenBackground"


def run(command: list[str], cwd: str | None = None) -> int:
    if sys.platform != "win32":
        return subprocess.call(command, cwd=cwd)

    import ctypes
    from ctypes import wintypes as wt

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    class StartupInfo(ctypes.Structure):
        _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                    ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
                    ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                    ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                    ("lpReserved2", ctypes.c_void_p), ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                    ("hStdError", wt.HANDLE)]

    class ProcessInformation(ctypes.Structure):
        _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE), ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]

    user32.CreateDesktopW.restype = wt.HANDLE
    user32.CreateDesktopW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.c_void_p]
    kernel32.GetStdHandle.restype = wt.HANDLE
    kernel32.GetStdHandle.argtypes = [wt.DWORD]
    kernel32.SetHandleInformation.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD]
    kernel32.CreateProcessW.argtypes = [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wt.BOOL, wt.DWORD,
                                        ctypes.c_void_p, wt.LPCWSTR, ctypes.POINTER(StartupInfo),
                                        ctypes.POINTER(ProcessInformation)]
    kernel32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
    kernel32.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
    kernel32.CloseHandle.argtypes = [wt.HANDLE]

    generic_all = 0x10000000
    # The desktop lives while a handle or a process on it does; a later run reopens it by name.
    if not user32.CreateDesktopW(DESKTOP, None, None, 0, generic_all, None):
        raise ctypes.WinError(ctypes.get_last_error())

    startf_usestdhandles, handle_flag_inherit = 0x100, 0x1
    info = StartupInfo()
    info.cb = ctypes.sizeof(info)
    info.lpDesktop = "WinSta0\\" + DESKTOP
    info.dwFlags = startf_usestdhandles
    sys.stdout.flush()
    sys.stderr.flush()
    for field, std in (("hStdInput", -10), ("hStdOutput", -11), ("hStdError", -12)):
        handle = kernel32.GetStdHandle(wt.DWORD(std & 0xFFFFFFFF))
        if handle and handle != wt.HANDLE(-1).value:
            kernel32.SetHandleInformation(handle, handle_flag_inherit, handle_flag_inherit)
        setattr(info, field, handle)
    made = ProcessInformation()
    line = ctypes.create_unicode_buffer(subprocess.list2cmdline(command))
    if not kernel32.CreateProcessW(None, line, None, None, True, 0, None, cwd, ctypes.byref(info), ctypes.byref(made)):
        raise ctypes.WinError(ctypes.get_last_error())
    kernel32.CloseHandle(made.hThread)
    try:
        kernel32.WaitForSingleObject(made.hProcess, 0xFFFFFFFF)
    except KeyboardInterrupt:
        subprocess.call(["taskkill", "/PID", str(made.dwProcessId), "/T", "/F"], stdout=subprocess.DEVNULL)
        raise
    code = wt.DWORD()
    kernel32.GetExitCodeProcess(made.hProcess, ctypes.byref(code))
    kernel32.CloseHandle(made.hProcess)
    return code.value


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(run(sys.argv[1:]))
