import gzip
import json
import stat
import struct
import sys
from hashlib import sha256
from pathlib import Path
from zipfile import ZipFile

sys.path.insert(0, str(Path("firmware").resolve()))
from prepare_diagnostic import make_cpio, read_cpio, read_uimage

ROOT = Path(__file__).resolve().parent
with ZipFile(ROOT / "firmware-x70-2023-reference.zip") as z:
    fat = z.read("x70/recovery.img")


def recover_file(name):
    sec, spc, rsv, copies, root_count = struct.unpack_from("<HBHBH", fat, 11)
    fsize = struct.unpack_from("<H", fat, 22)[0]
    first = (rsv + copies * fsize) * sec
    data_start = first + ((root_count * 32 + sec - 1) // sec) * sec
    ent = next(
        fat[i : i + 32]
        for i in range(first, first + root_count * 32, 32)
        if fat[i : i + 11] == name and fat[i + 11] != 15
    )
    current, size = struct.unpack_from("<HI", ent, 26)
    result = bytearray()
    visited = set()
    while len(result) < size:
        assert 2 <= current < 0xFFF0 and current not in visited
        visited.add(current)
        at = data_start + (current - 2) * spc * sec
        result += fat[at : at + spc * sec]
        current = struct.unpack_from("<H", fat, rsv * sec + 2 * current)[0]
    return bytes(result[:size])


kernel = recover_file(b"UIMAGE     ")
dtb = recover_file(b"KERNEL  DTB")
ota = recover_file(b"OTA_INITIMG")
_, k = read_uimage(kernel)
_, raw = read_uimage(ota)

build = ROOT / "x70-recovery-ram"
KERNEL_AT = 0x01000000
INITRD_AT = 0x01800000
DTB_AT = 0x01E00000
LIMIT = 0x02000000
assert struct.unpack_from("<I", k, 0x24)[0] == 0x016F2818
assert len(k) + KERNEL_AT < INITRD_AT


def ram_dtb(blob, initrd_len):
    header = list(struct.unpack_from(">10I", blob))
    (
        magic,
        total,
        structure,
        strings,
        _reserve,
        version,
        _last,
        _cpu,
        str_size,
        dt_size,
    ) = header
    assert magic == 0xD00DFEED and total == len(blob) and version == 17
    names = bytearray(blob[strings : strings + str_size])
    tree = bytearray()
    pos = structure
    stack = []
    seen = set()
    storage_paths = set()
    changes = {
        ("/memory", "reg"): struct.pack(">II", 0, LIMIT),
        (
            "/chosen",
            "bootargs",
        ): b"console=ttyS5,115200 earlyprintk loglevel=8 rdinit=/init root=/dev/ram0 ro maxcpus=1 panic=0\0",
        ("/chosen", "linux,initrd-start"): struct.pack(">I", INITRD_AT),
        ("/chosen", "linux,initrd-end"): struct.pack(">I", INITRD_AT + initrd_len),
    }

    def emit_prop(name, value):
        name = name.encode() + b"\0"
        start = names.find(name)
        if start < 0:
            start = len(names)
            names.extend(name)
        tree.extend(struct.pack(">III", 3, len(value), start))
        tree.extend(value)
        tree.extend(bytes(-len(value) % 4))

    while pos < structure + dt_size:
        token = struct.unpack_from(">I", blob, pos)[0]
        begin = pos
        pos += 4
        if token == 1:
            end = blob.index(0, pos)
            node = blob[pos:end].decode()
            if stack and stack[-1][0] in ("/chosen", "/memory"):
                raise ValueError("Expected a leaf boot-time DT node")
            path = "/".join([entry[1] for entry in stack] + [node]) or "/"
            stack.append((path, node))
            if node.startswith(("nand@", "mmc@")):
                changes[(path, "status")] = b"disabled\0"
                storage_paths.add(path)
            pos = (end + 4) & ~3
        elif token == 2:
            path, _ = stack.pop()
            for (node, name), value in changes.items():
                if node == path and (node, name) not in seen:
                    emit_prop(name, value)
                    seen.add((node, name))
        elif token == 3:
            length, idx = struct.unpack_from(">II", blob, pos)
            pos += 8
            name = blob[strings + idx : blob.index(0, strings + idx)].decode()
            pos = (pos + length + 3) & ~3
            key = (stack[-1][0], name)
            if key in changes:
                emit_prop(name, changes[key])
                seen.add(key)
                continue
        elif token == 9:
            assert not stack
        elif token != 4:
            raise ValueError(f"Unknown DT token {token}")
        tree.extend(blob[begin:pos])
        if token == 9:
            break
    assert seen == set(changes) and len(storage_paths) >= 4
    header[1] = structure + len(tree) + len(names)
    header[3] = structure + len(tree)
    header[8:10] = [len(names), len(tree)]
    result = struct.pack(">10I", *header) + blob[40:structure] + tree + names
    assert len(result) == header[1]
    return result, sorted(storage_paths)


entries = read_cpio(gzip.decompress(raw))
init = b"""#!/bin/busybox sh
export PATH=/bin
exec </dev/console >/dev/console 2>&1
echo X70-RECOVERY-RAM-INIT-OK
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox uname -a
(
    /bin/busybox sleep 2
    for node in /sys/class/backlight/* /sys/class/leds/*; do
        [ -w "$node/brightness" ] || continue
        original=$(/bin/busybox cat "$node/brightness") || continue
        high=$(/bin/busybox cat "$node/max_brightness") || continue
        [ "$high" -gt 0 ] || continue
        for pulse in 1 2 3; do
            echo 0 >"$node/brightness"
            /bin/busybox sleep 1
            echo "$high" >"$node/brightness"
            /bin/busybox sleep 1
        done
        echo "$original" >"$node/brightness"
        break
    done
) &
while true; do
    /bin/busybox sh -i
    /bin/busybox sleep 1
done
"""
candidates = [
    (name, stat.S_IFDIR | 0o755, b"", 0, 0)
    for name in ("bin", "lib", "proc", "sys", "dev", "tmp")
]
candidates.extend(
    [
        ("init", stat.S_IFREG | 0o755, init, 0, 0),
        ("dev/console", stat.S_IFCHR | 0o600, b"", 5, 1),
        ("dev/null", stat.S_IFCHR | 0o666, b"", 1, 3),
    ]
)
for n, (a, d) in entries.items():
    if n == "bin/busybox" or (n.startswith("lib/") and "/" not in n[4:] and ".so" in n):
        assert stat.S_ISREG(a[1]) or stat.S_ISLNK(a[1])
        candidates.append((n, a[1], d.rstrip(b"\0") if stat.S_ISLNK(a[1]) else d, 0, 0))
cpio = make_cpio(candidates)
archive = read_cpio(cpio)
assert archive["init"][1] == init and not any(
    "nand" in n or n.endswith(".ko") for n in archive
)
assert archive["bin/busybox"][1] == entries["bin/busybox"][1]
assert all(
    n == "init" or n.startswith(("bin/", "lib/", "dev/")) or "/" not in n
    for n in archive
)
ramdisk = gzip.compress(cpio, mtime=0)
assert gzip.decompress(ramdisk) == cpio
assert INITRD_AT + len(ramdisk) < DTB_AT
ramtree, storage = ram_dtb(dtb, len(ramdisk))
assert DTB_AT + len(ramtree) < LIMIT
files = {"zImage": k, "ram-initramfs.cpio.gz": ramdisk, "ram-kernel.dtb": ramtree}
manifest = {
    "source": "x70/recovery.img from firmware-x70-2023-reference.zip",
    "kernel_container_sha256": sha256(kernel).hexdigest(),
    "original_initramfs_sha256": sha256(ota).hexdigest(),
    "original_dtb_sha256": sha256(dtb).hexdigest(),
    "files": {name: sha256(data).hexdigest() for name, data in files.items()},
    "physical_addresses": {
        "zImage": KERNEL_AT,
        "ram-initramfs.cpio.gz": INITRD_AT,
        "ram-kernel.dtb": DTB_AT,
    },
    "ram_limit": LIMIT,
    "disabled_dt_storage_nodes": storage,
    "boot_marker": "X70-RECOVERY-RAM-INIT-OK",
}
build.mkdir(exist_ok=True)
for name, data in files.items():
    (build / name).write_bytes(data)
(build / "manifest.json").write_text(
    json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
)
print(
    "RAM Recovery artifacts",
    build,
    "image sizes",
    [len(data) for data in files.values()],
)
