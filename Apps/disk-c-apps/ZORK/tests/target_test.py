#!/usr/bin/env python3
"""Play the actual dcc binary under ntvcm in a temporary working directory."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ntvcm", type=Path)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    runner, executable = args.ntvcm.resolve(), args.executable.resolve()
    route_file = Path(__file__).with_name("walkthrough.txt")
    route = "\r".join(route_file.read_text().splitlines()) + "\r\r"
    # Exercise failed and repeated actions in the real compiled playthrough.
    route = route.replace("open case\r", "take gear\ropen case\ropen case\r")
    route = route.replace("light lamp\r", "light lamp\rextinguish lamp\rlight lamp\r")
    route = route.replace("use oil on warden\r", "e\rwait\rwait\rn\ruse oil on warden\ruse oil on warden\r")
    route = route.replace("turn valve\r", "turn valve\rturn valve\r")
    route = route.replace("say dawn\r", "take crown\rsay midnight\rsay dawn\rsay dawn\r")
    warden_route = (
        "e\rtake lamp\re\rtake key\rw\rd\rlight lamp\rtake coin\ru\rw\rn\re\rd\r"
        "take journal\ru\rw\rs\re\re\rn\rgive journal to ghost\rtake flute\rs\rw\rd\r"
        "n\ropen slab\re\rn\rplay flute\re\rwait\rwait\rw\rwait\rwait\rq\r"
    )
    with tempfile.TemporaryDirectory(prefix="zork-target-") as directory:
        work = Path(directory)
        quit_output = None
        for name, keys in (("win", "\r" + route), ("quit", "\rQUIT\r"),
                           ("warden", "\r" + warden_route),
                           ("ctrl-s-startup", "\x13\x04\rQUIT\r"),
                           ("intro-quit", "q"), ("ctrl-c", "\x03"),
                           ("in-game-ctrl-c", "\r\x03")):
            result = subprocess.run(
                # ntvcm translates redirected LF to CP/M CR. Avoid its CR
                # look-ahead consuming the final confirmation at pipe EOF.
                [str(runner), "-c", str(executable)],
                input=keys.replace("\r", "\n").encode("ascii"),
                cwd=work, capture_output=True, timeout=20, check=True,
            )
            assert not result.stderr, result.stderr.decode(errors="replace")
            output = result.stdout
            assert b"ZORK / CP-M" in output
            assert output.count(b"Z O R K") == 1
            assert b"On a forgotten coast" in output and b"Press ENTER to begin" in output
            assert b"\x1b[100m" in output and b"\x1b[107m" in output
            assert output.endswith(b"\x1b[0m\x1b[?7h\x1b[?25h\x1b[30;1H\r\n")
            if name == "win":
                assert b"YOU HAVE WON!" in output
                assert b"SCORE:300/300" in output
            else:
                assert b"Goodbye, adventurer." in output
                assert b"YOU HAVE WON!" not in output
            if name == "warden":
                assert b"lullaby stills the warden" in output
                assert b"gently escorts you" in output
            if name == "quit":
                quit_output = output
            elif name == "ctrl-s-startup":
                # Queued Left/Right must neither stall drawing nor alter QUIT.
                assert output == quit_output
            print(f"Z80/CP-M {name} PASS ({len(output)} output bytes)")
    print(f"ZORK target PASS: {executable.stat().st_size} byte COM")


if __name__ == "__main__":
    main()
