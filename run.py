"""Run the reproducer in an isolated nested Niri instance."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time


def main():
    root = Path(__file__).resolve().parent
    env = os.environ.copy()
    if not env.get("WAYLAND_DISPLAY"):
        raise SystemExit("Run this from an existing Wayland session.")
    for name in ("probe-cycles", "ime-recreate", "keys"):
        if not (root / "bin" / name).is_file():
            raise SystemExit("Run bash build.sh first.")
    out = Path(tempfile.mkdtemp(prefix="ime-popup-repro-"))
    env["REPRO_PARENT_DISPLAY"] = env["WAYLAND_DISPLAY"]
    env.pop("NIRI_SOCKET", None)
    print("Evidence directory:", out, flush=True)
    with (out / "niri.log").open("w") as log:
        child = subprocess.Popen(
            ["niri", "-c", str(root / "niri.kdl"), "--", "python3",
             str(root / "worker.py"), str(out)],
            env=env, stdout=log, stderr=log, start_new_session=True,
        )
        try:
            deadline = time.monotonic() + 120
            while not (out / "done").exists():
                if child.poll() is not None:
                    raise RuntimeError("Nested Niri exited; inspect niri.log")
                if time.monotonic() >= deadline:
                    raise TimeoutError("Reproducer timed out; inspect the evidence directory")
                time.sleep(.5)
            results = (out / "results.jsonl").read_text()
            print(results, end="")
            if json.loads(results.splitlines()[-1]).get("status") != "complete":
                raise RuntimeError("Reproducer failed; inspect the evidence directory")
        finally:
            try:
                os.killpg(child.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()


if __name__ == "__main__":
    main()
