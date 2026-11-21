"""Package a matched XGB35 kernel module and native Alpine PowerVR userspace."""
import argparse
import gzip
import hashlib
import io
from pathlib import Path
import tarfile


def build(module, target, output):
    libraries = ["libEGL", "libGLESv2", "libpvrWSEGL", "libpvrWSEGL_fbdev", "libIMGegl_SGX540_130",
                 "libglslcompiler_SGX540_130", "libusc_SGX540_130", "libsrv_um_SGX540_130",
                 "libsrv_init_SGX540_130"]
    files = {"lib/" + n + ".so": (target / (n + ".so")).read_bytes() for n in libraries}
    files["pvrsrvctl"] = (target / "pvrsrvctl").read_bytes()
    files["pvrsrvkm_sgx540_130.ko"] = module.read_bytes()
    for name, data in files.items():
        if data[:6] != b"\x7fELF\x01\x01" or data[18:20] != b"\x28\x00":
            raise ValueError(f"{name} must be a little-endian 32-bit ARM ELF")
        if b"GLIBC_" in data or b"libc.so.6\0" in data:
            raise ValueError(f"{name} uses glibc")
    if b"/lib/ld-musl-armhf.so.1\0" not in files["pvrsrvctl"]:
        raise ValueError("pvrsrvctl must use the Alpine ARM hard-float interpreter")
    files["sgx-run"] = Path(__file__).with_name("sgx-run").read_bytes().replace(b"\r\n", b"\n")
    files["powervr.ini"] = b"[default]\nParamBufferSize=262144\nMaxParamBufferSize=262144\n"
    files["powervr-fbdev.ini"] = files["powervr.ini"] + b"WindowSystem=libpvrWSEGL_fbdev.so\n"
    files["SHA256SUMS"] = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                                  for name, data in sorted(files.items())).encode()
    with output.open("wb") as file, gzip.GzipFile(filename="", fileobj=file, mode="wb", mtime=0) as compressed, \
            tarfile.open(fileobj=compressed, mode="w") as tar:
        for name, data in sorted(files.items()):
            info = tarfile.TarInfo(name)
            info.mode = 0o755 if name in ("pvrsrvctl", "sgx-run") else 0o644
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
        for link, dest in [("libEGL.so.1", "libEGL.so"), ("libGLESv2.so.2", "libGLESv2.so")]:
            info = tarfile.TarInfo("lib/" + link)
            info.type, info.linkname = tarfile.SYMTYPE, dest
            tar.addfile(info)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("module", "target", "output"):
        parser.add_argument(key, type=Path)
    args = parser.parse_args()
    build(args.module, args.target, args.output)
