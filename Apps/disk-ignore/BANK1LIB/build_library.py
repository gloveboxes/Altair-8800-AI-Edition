"""Build the fixed-origin SPREADS.LIB using DCC and the local L80CC."""

from pathlib import Path
import json
import os
import subprocess

root = Path(__file__).resolve().parents[2]
source = Path(__file__).resolve().parent
dcc = Path(os.environ.get("DCC_DIR", str(root.parent / "dcc")))
l80cc_value = os.environ.get("DCC_L80CC")
if not l80cc_value:
    raise SystemExit("DCC_L80CC must point to the fixed-address L80CC executable")
l80cc = Path(l80cc_value).expanduser()
if not l80cc.is_file() or not os.access(l80cc, os.X_OK):
    raise SystemExit(f"DCC_L80CC is not an executable file: {l80cc}")
output = source / "build"
output.mkdir(exist_ok=True)
subprocess.run(["python3", str(source / "generate_host_api.py")], check=True)
interface = json.loads((source / "interface.json").read_text())
library_name = interface["library"]
export_args = []
for entry in interface["exports"]:
    name, symbol = entry["name"], entry["symbol"]
    export_args += ["--export", name if name == symbol else name + "=" + symbol]
subprocess.run([str(dcc / "dcc"), "-c", str(source / "LIB.C"), "-o",
                str(output / "LIB.MAC")], check=True)
rtl_source = (dcc / "DCCRTL.MAC").read_text()
constants = rtl_source[rtl_source.index("HDRSIZE"):rtl_source.index("        cseg")]
body = rtl_source[rtl_source.index("        public  __wr_hf"):]
exit_entry = body.index("_exit:")
exit_state = body.index("__ae_hook:", exit_entry)
body = body[:exit_entry] + "_exit:\n        ld hl,0\n        ret\n" + body[exit_state:]
bank_start = ("; Bank-1 runtime: one fixed 4 KiB C heap and 2 KiB C stack.\n"
              "        cseg\n"
              "__hstart equ 0b000h\n"
              "__stack_size equ 0800h\n"
              "        public __brk\n"
              "        public __hlimit\n"
              "__brk: dw 0\n"
              "__hlimit: dw 0\n")
(output / "RTLBK1.MAC").write_text(constants + bank_start + body)
subprocess.run([str(dcc / "dccrtlstrip"), "-r", "RTLBK1.MAC",
                "-o", "RTLMIN.MAC", "LIB.MAC"], cwd=output, check=True)
minimal = (output / "RTLMIN.MAC").read_text()
if "start:" in minimal or "jp      0000h" in minimal:
    raise SystemExit("unsafe transient startup path in bank-1 runtime")
(output / "RTLMIN.MAC").write_text(minimal.replace("\tend\tstart", "\tend"))
for module in ("LIB", "RTLMIN"):
    subprocess.run([str(dcc / "m80c"), f"={module}.MAC", "/X", "/O", "/Z"],
                   cwd=output, check=True)
subprocess.run([str(l80cc), "/P:100,RTLMIN,LIB", "--lib",
                *export_args,
                "-o", str(source / library_name)],
               cwd=output, check=True)
print(source / (library_name + ".LIB"))
