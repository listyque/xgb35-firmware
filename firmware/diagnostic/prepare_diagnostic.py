"""Package the inspected vendor kernel with a standalone, module-free initramfs."""

from pathlib import Path, PurePosixPath
import gzip
import hashlib
import json
import stat
import struct
import zipfile
import zlib


HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / "build" / "firmware-inspection" / "misc"
OUTPUT = HERE / "atm7051h-diagnostic"
EXPECTED = {
    "uImage": "655ac99b3648ae19c515355406cc379c254e6413befc439811a00d9920f46e42",
    "kernel.dtb": "642c4ff13655a37462f8fbab0779187b4ec6b7b6438c674fc321681c88d208c2",
    "ramdisk.img": "2f29ba24fcba7536fc4ab04d594c88d6981feb00c88e6b510480fc5ab41d7e40",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_uimage(data):
    require(len(data) >= 64, "Truncated uImage header")
    fields = struct.unpack(">7I4B32s", data[:64])
    require(fields[0] == 0x27051956, "Invalid uImage magic")
    header = bytearray(data[:64])
    header[4:8] = bytes(4)
    require(zlib.crc32(header) == fields[1], "uImage header CRC mismatch")
    require(len(data) == 64 + fields[3], "uImage size mismatch")
    payload = data[64:]
    require(zlib.crc32(payload) == fields[6], "uImage data CRC mismatch")
    require(fields[7:9] == (5, 2), "Expected Linux / ARM uImage")
    return fields, payload


def make_ramdisk(payload, original_header):
    fields = list(original_header)
    fields[1:4] = [0, 0, len(payload)]
    fields[6] = zlib.crc32(payload)
    fields[11] = b"ATM7051H diagnostic".ljust(32, b"\0")
    header = struct.pack(">7I4B32s", *fields)
    fields[1] = zlib.crc32(header)
    return struct.pack(">7I4B32s", *fields) + payload


def read_cpio(data):
    """Read newc records in memory; never extract paths from a source archive."""
    entries = {}
    offset = 0
    while offset + 110 <= len(data):
        header = data[offset:offset + 110]
        require(header[:6] == b"070701", "Expected newc CPIO archive")
        fields = [int(header[i:i + 8], 16) for i in range(6, 110, 8)]
        size, namesize = fields[6], fields[11]
        start = offset + 110
        require(namesize > 0 and start + namesize <= len(data), "Invalid CPIO name size")
        require(data[start + namesize - 1] == 0, "Unterminated CPIO name")
        name = data[start:start + namesize - 1].decode("utf-8")
        path = PurePosixPath(name)
        require(name and not path.is_absolute() and ".." not in path.parts,
                "Unsafe CPIO path")
        start = (start + namesize + 3) & ~3
        require(start + size <= len(data), "Truncated CPIO payload")
        if name == "TRAILER!!!":
            require(size == 0 and not any(data[start:]), "Invalid CPIO trailer")
            return entries
        require(name not in entries, "Duplicate CPIO path")
        entries[name] = (fields, data[start:start + size])
        offset = (start + size + 3) & ~3
    raise ValueError("Missing CPIO trailer")


def make_cpio(entries):
    result = bytearray()
    for inode, (name, mode, payload, major, minor) in enumerate(
            [*entries, ("TRAILER!!!", 0, b"", 0, 0)], 1):
        encoded_name = name.encode() + b"\0"
        fields = [inode, mode, 0, 0, 2 if stat.S_ISDIR(mode) else 1, 0,
                  len(payload), 0, 0, major, minor, len(encoded_name), 0]
        result.extend(b"070701" + b"".join(f"{v:08x}".encode() for v in fields))
        result.extend(encoded_name)
        result.extend(bytes(-len(result) % 4))
        result.extend(payload)
        result.extend(bytes(-len(result) % 4))
    result.extend(bytes(-len(result) % 512))
    return bytes(result)


def check_static_arm_elf(data):
    require(len(data) >= 52 and data[:6] == b"\x7fELF\x01\x01",
            "Expected little-endian ELF32 BusyBox")
    require(struct.unpack_from("<H", data, 18)[0] == 40, "BusyBox is not ARM")
    offset = struct.unpack_from("<I", data, 28)[0]
    size, count = struct.unpack_from("<HH", data, 42)
    require(size >= 32 and count > 0 and offset + size * count <= len(data),
            "Invalid ELF program headers")
    types = [struct.unpack_from("<I", data, offset + i * size)[0] for i in range(count)]
    require(1 in types and 3 not in types, "BusyBox needs an external dynamic loader")
    for applet in (b"sh", b"mount", b"mdev", b"setsid", b"cttyhack", b"dmesg"):
        require(applet + b"\0" in data, "Missing expected BusyBox applet string")


def self_check(cpio, ramdisk, entries):
    parsed = read_cpio(cpio)
    require(set(parsed) == {e[0] for e in entries}, "CPIO file list mismatch")
    for name, mode, payload, major, minor in entries:
        fields, actual = parsed[name]
        require((fields[1], actual, fields[9], fields[10]) == (mode, payload, major, minor),
                "CPIO contents or device numbers changed")
        require(fields[2:4] == [0, 0], "CPIO entries must belong to root")
    fields, compressed = read_uimage(ramdisk)
    require(fields[9:11] == (3, 1), "Expected gzip ramdisk uImage")
    require(gzip.decompress(compressed) == cpio, "Initramfs round trip failed")
    for label, check in (
        ("payload CRC", lambda: read_uimage(ramdisk[:-1] + bytes([ramdisk[-1] ^ 1]))),
        ("header CRC", lambda: read_uimage(ramdisk[:8] + bytes([ramdisk[8] ^ 1]) + ramdisk[9:])),
        ("truncated archive", lambda: read_cpio(cpio[:200])),
        ("unsafe path", lambda: read_cpio(make_cpio([("../bad", stat.S_IFREG | 0o644, b"", 0, 0)]))),
    ):
        try:
            check()
        except ValueError:
            continue
        raise ValueError(f"Self-check did not reject {label}")


def main():
    sources = {name: (SOURCE / name).read_bytes() for name in EXPECTED}
    for name, expected in EXPECTED.items():
        require(sha256(sources[name]) == expected, f"Unexpected source file: {name}")
    kernel_header, _ = read_uimage(sources["uImage"])
    require(kernel_header[9] == 2, "Expected kernel image")
    original_header, compressed = read_uimage(sources["ramdisk.img"])
    require(original_header[9:11] == (3, 1), "Expected gzip source ramdisk")
    original = read_cpio(gzip.decompress(compressed))
    busybox = original["bin/busybox"][1]
    check_static_arm_elf(busybox)
    dtb = sources["kernel.dtb"]
    require(len(dtb) >= 40 and struct.unpack_from(">II", dtb) == (0xD00DFEED, len(dtb)),
            "Invalid DTB header or size")
    require(b"console=ttyS5,115200" in dtb, "Unexpected UART console in DTB")

    entries = [(name, stat.S_IFDIR | mode, b"", 0, 0) for name, mode in (
        ("bin", 0o755), ("dev", 0o755), ("proc", 0o755), ("sys", 0o755),
        ("tmp", 0o1777), ("root", 0o700), ("mnt", 0o755), ("etc", 0o755))]
    entries.extend([
        ("dev/console", stat.S_IFCHR | 0o600, b"", 5, 1),
        ("dev/null", stat.S_IFCHR | 0o666, b"", 1, 3),
        ("dev/tty", stat.S_IFCHR | 0o666, b"", 5, 0),
        ("bin/busybox", stat.S_IFREG | 0o755, busybox, 0, 0),
        ("bin/sh", stat.S_IFLNK | 0o777, b"busybox", 0, 0),
    ])
    for name, source_name in (("init", "init"), ("bin/diagnose", "diagnose")):
        payload = (HERE / source_name).read_text(encoding="utf-8").replace("\r\n", "\n").encode()
        require(payload.startswith(b"#!/bin/"), "Missing initramfs script interpreter")
        entries.append((name, stat.S_IFREG | 0o755, payload, 0, 0))
    cpio = make_cpio(entries)
    ramdisk = make_ramdisk(gzip.compress(cpio, mtime=0), original_header)
    self_check(cpio, ramdisk, entries)

    files = {"uImage": sources["uImage"], "kernel.dtb": dtb, "ramdisk.img": ramdisk,
             "README.md": (HERE / "README.md").read_bytes()}
    manifest = {
        "purpose": "Experimental RAM diagnostic, NOT a factory firmware or NAND image",
        "hardware_tested": False,
        "target_board": "LLW7051-057-MAIN-BOARD V2.0 (compatibility unverified)",
        "source_update_sha256": "4db764e126b89fb82c607322061089d379bd2e6feb416fb1f58a8d086aae5f1e",
        "source_version": "2.0.00.240519.1736",
        "source_board_label": "057V1_vLCD0_V1.0",
        "kernel": "Linux 3.10.52 #2 SMP PREEMPT Sun May 19 17:25:16 HKT 2024",
        "busybox": "1.14.3, statically linked ARM ELF32, from source ramdisk",
        "source_files_sha256": EXPECTED,
        "runtime_files": {name: {"bytes": len(data), "sha256": sha256(data)}
                          for name, data in files.items()},
        "initramfs_entries": [e[0] for e in entries],
        "checks_passed": ["source SHA256", "uImage header/data CRC32", "ARM ELF headers",
                          "CPIO contents, modes, root ownership, device nodes",
                          "gzip round trip", "corrupt/truncated/unsafe input rejection"],
        "not_tested": ["hardware boot", "shell execution on ARM", "board DTB compatibility",
                       "vendor kernel storage probe behavior", "U-Boot transfer/RAM addresses"],
    }
    files["manifest.json"] = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (OUTPUT / name).write_bytes(data)
    archive = OUTPUT.with_suffix(".zip")
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as package:
        for name in sorted(files):
            info = zipfile.ZipInfo(f"{OUTPUT.name}/{name}", date_time=(2024, 5, 19, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            package.writestr(info, files[name])
    with zipfile.ZipFile(archive) as package:
        require(package.testzip() is None, "ZIP verification failed")
        for name, data in files.items():
            require(package.read(f"{OUTPUT.name}/{name}") == data, "ZIP contents mismatch")
    print(f"Prepared: {OUTPUT}")
    print(f"Archive: {archive} ({archive.stat().st_size:,} bytes)")
    print("Image/container self-checks passed; hardware boot has NOT been tested.")


if __name__ == "__main__":
    main()
