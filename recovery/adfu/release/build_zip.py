"""Build the Windows x64 ADFU recovery release ZIP from the repository."""
import argparse
from hashlib import file_digest
from pathlib import Path, PurePosixPath
from zipfile import ZIP_DEFLATED, ZipFile


HERE = Path(__file__).resolve().parent
ADFU = HERE.parent
REPO = ADFU.parents[1]
PYTHON_SHA256 = "9e35319bffedab8b36ad44d8f377cb4f1733bcec257a0b6d04d94f01486ddb38"
TOOLS = (
    "adfu_autonomous_service.py",
    "adfu_nand_cache_dma_probe.py",
    "adfu_nand_write_ram_test.py",
    "adfu_read_nand.py",
    "adfu_resume_block0.py",
    "adfu_write_nand_block0.py",
    "build_autonomous_service.py",
    "nand_backup_format.py",
    "prepare_nand_write.py",
    "rom_usb.py",
    "service_nand_raw_probe.py",
    "service_nand_rom_probe.py",
    "service_usb.py",
    "simulate_nand_write.py",
    "verify_boot_ecc.py",
    "atm7051h-high-rom-ffff0000-ffff7fff.bin",
    "xgb35-mbrec-recovered.bin",
    "adfu-nand-first-256-pages-raw-with-oob.bin",
    "adfu-nand-blocks-1-3-raw-with-oob.bin",
)
WRITE_FILES = (
    "block0-restore-ECC-corrected.bin",
    "block0-row4-resume-ecc.json",
    "nand-boot-recovery.zip",
    "recovery-archive.sha256",
    "write-plan.json",
)
PHOTOS = ("adfu-close.png", "board-detail.png", "nand-detail.png")


def pack(python_zip, output):
    with python_zip.open("rb") as stream:
        if file_digest(stream, "sha256").hexdigest() != PYTHON_SHA256:
            raise ValueError("Expected the official Python 3.13.17 Windows x64 embeddable ZIP")
    with ZipFile(python_zip) as python, ZipFile(output, "x", ZIP_DEFLATED) as kit:
        for member in python.infolist():
            path = PurePosixPath(member.filename)
            if member.is_dir():
                continue
            if len(path.parts) != 1 or path.name != member.filename:
                raise ValueError(f"Unexpected path in Python ZIP: {member.filename}")
            data = python.read(member)
            if path.suffix == "._pth":
                data = b"python313.zip\r\n.\r\n../tools\r\n"
            kit.writestr(f"python/{path.name}", data)
        kit.writestr("START.cmd", (HERE / "START.cmd").read_text(encoding="ascii").replace("\n", "\r\n"))
        kit.write(HERE / "START-HERE.ru.html", "START-HERE.ru.html")
        kit.write(HERE / "recover.py", "tools/recover.py")
        for name in TOOLS:
            kit.write(ADFU / name, f"tools/{name}")
        for name in PHOTOS:
            kit.write(REPO / "photos" / name, f"photos/{name}")
        for name in ("adfuwinusb.inf", "adfuwinusb.cat"):
            kit.write(ADFU / "adfu-driver" / name, f"tools/adfu-driver/{name}")
        for name in ("manifest.json", "ddr-init.bin", "parameters.bin", "start.bin", "usb-packed.bin"):
            path = f"autonomous-service-diagnostic/{name}"
            kit.write(ADFU / path, f"tools/{path}")
        for name in WRITE_FILES:
            path = f"nand-write-preparation/{name}"
            kit.write(ADFU / path, f"tools/{path}")
        for path in sorted((ADFU / "nand-write-preparation/decoded").glob("rom-flags2-row*-sector*-sector.bin")):
            kit.write(path, f"tools/{path.relative_to(ADFU).as_posix()}")
        technical = (ADFU / "README.ru.md").read_text(encoding="utf-8")
        repo_url = "https://github.com/listyque/xgb35-firmware/blob/main/"
        technical = technical.replace("(README.md)", f"({repo_url}recovery/adfu/README.md)")
        technical = technical.replace("(../../photos/", "(../photos/")
        for directory in ("docs", "firmware"):
            technical = technical.replace(f"(../../{directory}/", f"({repo_url}{directory}/")
        kit.writestr("tools/TECHNICAL.ru.md", technical)
        kit.write(ADFU / "atm7051h-bootrom-nand.md", "tools/atm7051h-bootrom-nand.md")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("python_zip", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    pack(args.python_zip, args.output)
