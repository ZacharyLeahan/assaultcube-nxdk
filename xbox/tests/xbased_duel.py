"""Start the Xbox duel and capture a frame with xbased's Windows developer build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import tempfile
import time

from PIL import Image


def wait_until(read, accept, timeout, description):
    deadline = time.monotonic() + timeout
    while True:
        value = read()
        if accept(value):
            return value
        if time.monotonic() >= deadline:
            raise RuntimeError(f"Timed out waiting for {description}. Last result: {value}")
        time.sleep(0.1)


def run(exe, iso, output, timeout, profile_seconds):
    # xbased stores logs and configuration beside the executable on Windows.
    # Keep each test's executable and virtual disk isolated from the user's.
    output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="duel-", dir=output))
    print(f"Artifacts: {work}", flush=True)
    binary = work / exe.name
    shutil.copy2(exe, binary)
    for dll in exe.parent.glob("*.dll"):
        shutil.copy2(dll, work / dll.name)
    with socket.socket() as reserved:
        reserved.bind(("127.0.0.1", 0))
        port = reserved.getsockname()[1]
    token = secrets.token_hex(24)
    env = dict(os.environ, XBASED_DEVCTL_PORT=str(port), XBASED_DEVCTL_TOKEN=token)
    with (work / "host.log").open("w") as log:
        proc = subprocess.Popen(
            [binary, iso, "fullscreen=0", "net=off", "avpack=hdtv", "video=b0000", "aspect=16:9", "scale=1"],
            cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT,
        )
        try:
            def connect():
                if proc.poll() is not None:
                    raise RuntimeError(f"xbased exited with code {proc.returncode}")
                try:
                    return socket.create_connection(("127.0.0.1", port), timeout=0.5)
                except (ConnectionRefusedError, TimeoutError):
                    return None

            with wait_until(connect, bool, timeout, "developer control") as conn:
                conn.settimeout(timeout)
                with conn.makefile("rwb") as stream, iso.open("rb") as iso_file, exe.open("rb") as exe_file:
                    def command(line):
                        stream.write(line.encode() + b"\n")
                        stream.flush()
                        response = stream.readline()
                        if not response:
                            raise RuntimeError("xbased disconnected. See xbased.log.")
                        reply = json.loads(response)
                        if not reply["ok"]:
                            raise RuntimeError(reply.get("error", "command failed"))
                        return reply

                    def save(name, data):
                        (work / name).write_text(json.dumps(data, indent=2), encoding="utf-8")

                    save("run.json", {"iso_sha256": hashlib.file_digest(iso_file, "sha256").hexdigest(),
                                      "emulator_sha256": hashlib.file_digest(exe_file, "sha256").hexdigest()})
                    command("auth " + token)

                    def stats():
                        data = command("stats")
                        save("last-stats.json", data)
                        return data

                    wait_until(stats, lambda s: s["draws_total"] > 100, timeout, "game draws")
                    command("act START")
                    time.sleep(1)
                    if profile_seconds:
                        command(f"act prof {profile_seconds}")
                    command("act RT*0.3")
                    command("act LRIGHT=0.7*0.5")
                    time.sleep(max(1, profile_seconds + 0.5))
                    # capture accepts a whitespace-delimited filename relative to cwd.
                    command("capture draws.json")
                    command("shot " + str(work / "duel.bmp"))
                    wait_until(lambda: (work / "draws.json").is_file() and (work / "duel.bmp").is_file(),
                               bool, timeout, "frame capture")
                    command("pause")
                    save("threads.json", command("threads"))
                    save("frames.json", command("frames 0"))
                    save("status.json", command("status"))
                    target = command("step 1")["until_swap"]
                    stepped = wait_until(lambda: command("status"),
                                         lambda s: s["paused"] and s["swaps"] >= target, timeout, "one frame step")
                    save("stepped.json", stepped)
                    command("resume")
                    # The window can close before the control thread sends a reply.
                    stream.write(b"quit\n")
                    stream.flush()
            proc.wait(timeout=10)
            if proc.returncode:
                raise RuntimeError(f"xbased exited with code {proc.returncode}")
            with Image.open(work / "duel.bmp") as image:
                darkest, lightest = image.convert("L").getextrema()
                if lightest - darkest < 20:
                    raise RuntimeError("Captured frame has too little contrast to show gameplay")
                image.save(work / "duel.png")
            capture = json.loads((work / "draws.json").read_text())
            if not any(draw["status"] == "drawn" for draw in capture["draws"]):
                raise RuntimeError("No completed draws in capture")
            print(f"Captured {capture['draws_in_frame']} draws at frame {capture['frame']}")
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path, help="xbased.exe built with XBASED_DEVCTL=ON")
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, default=Path("xbox/.local/xbased"))
    parser.add_argument("--timeout", type=float, default=45)
    parser.add_argument("--profile", type=int, default=0, metavar="SECONDS",
                        help="sample the guest CPU during the duel and write samples to xbased.log")
    args = parser.parse_args()
    if args.profile < 0 or args.timeout <= 0:
        parser.error("profile must be nonnegative and timeout must be positive")
    run(args.exe.resolve(), args.iso.resolve(), args.output.resolve(), args.timeout, args.profile)
