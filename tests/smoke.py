#!/usr/bin/env python3
"""Read-only integration checks for the eight-device Ascend development host."""

import curses
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import pty
import re
import select
import shlex
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time


BINARY = Path(sys.argv[1] if len(sys.argv) > 1 else "build/astop").resolve()
LOGS = Path(tempfile.mkdtemp(prefix="astop-smoke-"))
ENV = dict(os.environ, TERM="xterm-256color", XDG_CONFIG_HOME=str(LOGS / "config"))


def real_settings():
    roots = {Path.home() / ".config" / "astop", Path.home() / ".config" / "nvtop"}
    if os.environ.get("XDG_CONFIG_HOME"):
        roots.update(Path(os.environ["XDG_CONFIG_HOME"]) / name for name in ("astop", "nvtop"))
    return {
        str(path): hashlib.sha256(path.read_bytes()).hexdigest()
        for root in roots
        for path in root.rglob("*")
        if path.is_file()
    }


def stop_child(child):
    if child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=5)


def command(*args):
    result = subprocess.run([str(BINARY), *args], env=ENV, capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, (args, result.returncode, result.stderr)
    return result.stdout


def check_snapshot(devices):
    assert isinstance(devices, list) and len(devices) == 8, "expected eight Ascend devices"
    assert [device["device_id"] for device in devices] == list(range(8))
    assert len({device["pci_bus_id"] for device in devices}) == 8
    for device in devices:
        assert isinstance(device, dict)
        assert "910B4-1" in device["device_name"], device["device_name"]
        memory = [None if device[key] is None else int(device[key])
                  for key in ("mem_total", "mem_used", "mem_free")]
        total, used, free = memory
        assert all(value is None or value >= 0 for value in memory), memory
        if total is not None:
            assert total > 0, memory
            assert all(value is None or value <= total for value in (used, free)), memory
        if None not in memory:
            assert used + free == total, memory
        for key in ("npu_util", "mem_util"):
            if device[key] is not None:
                assert 0 <= int(device[key].removesuffix("%")) <= 100, (key, device[key])
        for key in ("pcie_gen", "pcie_width", "pcie_rx_mb_per_ms", "pcie_tx_mb_per_ms",
                    "ecc_corrected", "ecc_uncorrected", "hccs_links"):
            value = device[key]
            assert value is None or type(value) is int and value >= 0, (key, value)
        for key in ("hccs_rx_bytes_per_second", "hccs_tx_bytes_per_second"):
            value = device[key]
            assert value is None or (type(value) in (int, float) and math.isfinite(value)
                                     and value >= 0), (key, value)
        assert device["hccs_healthy"] is None or type(device["hccs_healthy"]) is bool


def check_loop(stop_signal):
    child = subprocess.Popen([str(BINARY), "-l", "-d", "1"], env=ENV, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
    decoder = json.JSONDecoder()
    pending = ""
    snapshots = 0
    deadline = time.monotonic() + 15
    try:
        while snapshots < 2:
            assert time.monotonic() < deadline, "timed out waiting for two loop snapshots"
            ready, _, _ = select.select([child.stdout], [], [], 0.2)
            if not ready:
                assert child.poll() is None, "loop exited before producing two snapshots"
                continue
            chunk = os.read(child.stdout.fileno(), 65536)
            assert chunk, "loop closed stdout before producing two snapshots"
            pending += chunk.decode("utf-8")
            while pending.strip():
                pending = pending.lstrip()
                try:
                    devices, end = decoder.raw_decode(pending)
                except json.JSONDecodeError:
                    break
                check_snapshot(devices)
                snapshots += 1
                pending = pending[end:]
        child.send_signal(stop_signal)
        _, errors = child.communicate(timeout=5)
        assert child.returncode == 0, (stop_signal, child.returncode, errors.decode(errors="replace"))
    finally:
        stop_child(child)
        child.stdout.close()
        child.stderr.close()


def read_terminal(master, child, seconds):
    output = bytearray()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        ready, _, _ = select.select([master], [], [], min(0.1, max(0, deadline - time.monotonic())))
        if ready:
            try:
                chunk = os.read(master, 65536)
            except OSError as error:
                if error.errno == 5:  # Linux PTYs report EIO after their child closes.
                    break
                raise
            if not chunk:
                break
            output.extend(chunk)
        if child.poll() is not None:
            break
    return bytes(output)


def check_terminal(columns, rows):
    config = LOGS / f"config-{columns}x{rows}"
    environment = dict(ENV, XDG_CONFIG_HOME=str(config))
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
    child = subprocess.Popen([str(BINARY), "-d", "1"], env=environment, stdin=slave, stdout=slave,
                             stderr=slave, start_new_session=True)
    os.close(slave)
    transcript = bytearray()
    try:
        transcript.extend(read_terminal(master, child, 2))
        assert child.poll() is None, "terminal exited during startup"
        assert b"910B4" in transcript, "terminal did not show the real device name"
        assert b"Kill    " not in transcript, "process-signalling shortcut is visible"
        for key, expected in (("kf2", b"General Options"), ("kf2", None),
                              ("kf6", b"Sort by"), ("\n", None),
                              ("kf9", None), ("kf12", None)):
            sequence = key.encode() if key == "\n" else curses.tigetstr(key)
            assert sequence, f"terminal has no {key} sequence"
            os.write(master, sequence)
            output = read_terminal(master, child, 0.5)
            transcript.extend(output)
            assert child.poll() is None, f"terminal exited after {key}"
            if expected:
                assert expected in output, f"{key} did not show {expected!r}"
        assert b"Send signal:" not in transcript, "F9 opened a process-signalling menu"
        os.write(master, b"q")
        transcript.extend(read_terminal(master, child, 1))
        assert child.wait(timeout=5) == 0, "q did not exit successfully"
        assert (config / "astop" / "interface.ini").is_file(), "F12 did not save astop settings"
    finally:
        stop_child(child)
        os.close(master)
        (LOGS / f"pty-{columns}x{rows}.log").write_bytes(transcript)


def check_visible_layout():
    # A private tmux server interprets cursor movement, so captures contain visible cells only.
    def tmux(*args, check=True):
        return subprocess.run(["tmux", "-S", str(LOGS / "tmux.sock"), "-f", "/dev/null", *args],
                              env=ENV, capture_output=True, text=True, timeout=5, check=check).stdout

    def wait_visible(expected, label):
        deadline = time.monotonic() + 15
        while True:
            screen = tmux("capture-pane", "-p", "-t", "layout")
            (LOGS / f"screen-{label}.txt").write_text(screen)
            visible = set(re.findall(r"\bNPU\s+([0-7])\b", screen))
            if visible == set(expected):
                return screen
            assert time.monotonic() < deadline, f"{label}: visible device IDs {sorted(visible)}"
            time.sleep(0.1)

    def check_cards(screen, columns, details):
        lines = screen.splitlines()
        headers = [(row, match.start(), match[1]) for row, line in enumerate(lines)
                   for match in re.finditer(r"\bNPU\s+([0-7])\b", line)]
        assert len(headers) == 8, headers
        for row, column, device_id in headers:
            right = min((x for y, x, _ in headers if y == row and x > column), default=columns)
            bottom = min((y for y, _, _ in headers if y > row), default=len(lines))
            card = "\n".join(line[column:right] for line in lines[row:bottom])
            assert "910B4-1" in card, (device_id, card)
            for label in ("PCIe ", "ECC ", "HCCS "):
                assert (label in card) == details, (device_id, label, card)
            if details:
                assert len(re.findall(r"\bRX ", card)) == 2, (device_id, card)
                assert len(re.findall(r"\bTX ", card)) == 2, (device_id, card)
                assert card.count("MB/ms") == 2, (device_id, card)

    try:
        tmux("new-session", "-d", "-s", "layout", "-x", "160", "-y", "50",
             shlex.join([str(BINARY), "-d", "1"]))
        tmux("set-window-option", "-t", "layout", "remain-on-exit", "on")
        for columns, rows in ((160, 50), (80, 24)):
            if columns == 80:
                tmux("resize-window", "-t", "layout", "-x", str(columns), "-y", str(rows))
            assert tmux("display-message", "-p", "-t", "layout", "#{pane_width}x#{pane_height}").strip() == f"{columns}x{rows}"
            screen = wait_visible("01234567", f"{columns}x{rows}")
            check_cards(screen, columns, details=columns == 160)
        tmux("send-keys", "-t", "layout", "F2", "Down", "Down", "Down", "Down", "Right", "Enter", "F2")
        wait_visible("1234567", "device0-disabled")
        tmux("send-keys", "-t", "layout", "F12", "q")
        deadline = time.monotonic() + 5
        while tmux("display-message", "-p", "-t", "layout", "#{pane_dead}").strip() != "1":
            assert time.monotonic() < deadline, "q did not exit before configuration reload"
            time.sleep(0.1)
        assert (LOGS / "config" / "astop" / "interface.ini").is_file(), "subset configuration was not saved"
        tmux("send-keys", "-R", "-t", "layout")
        assert not re.search(r"\bNPU\s+[0-7]\b", tmux("capture-pane", "-p", "-t", "layout")), "old screen was not cleared"
        tmux("respawn-pane", "-t", "layout", shlex.join([str(BINARY), "-d", "1"]))
        wait_visible("1234567", "device0-disabled-reloaded")
        tmux("send-keys", "-t", "layout", "q")
    finally:
        tmux("kill-server", check=False)


def main():
    before = real_settings()
    print(f"Smoke logs: {LOGS}", flush=True)
    try:
        assert "astop" in command("--help").lower()
        assert "astop" in command("--version").lower()
        for args in (("-d", "1x"), ("-d", "9" * 40), ("unexpected-positional",)):
            result = subprocess.run([str(BINARY), "-s", *args], env=ENV, capture_output=True, timeout=5)
            assert result.returncode > 0, (args, result.returncode)
        check_snapshot(json.loads(command("-s")))
        print("PASS help, version, invalid CLI inputs, eight-device snapshot", flush=True)
        for stop_signal in (signal.SIGINT, signal.SIGTERM):
            check_loop(stop_signal)
        print("PASS loop arrays and graceful SIGINT/SIGTERM", flush=True)
        curses.setupterm(term=ENV["TERM"])
        for columns, rows in ((80, 24), (160, 50)):
            check_terminal(columns, rows)
            print(f"PASS terminal {columns}x{rows}, F2/F6/F9/F12, q", flush=True)
        check_visible_layout()
        print("PASS eight complete detail cards at 160x50, compact cards at 80x24, resize, saved device selection reload", flush=True)
    finally:
        assert real_settings() == before, "real user settings changed"
    print("PASS real user settings unchanged", flush=True)


if __name__ == "__main__":
    main()
