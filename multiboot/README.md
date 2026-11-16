# XGB35 Multiboot Installer Preview

**EN** · [RU](README.ru.md)

[Installer release](https://github.com/listyque/xgb35-firmware/releases/tag/multiboot-installer-v0.1.0)

This Windows tool prepares a **dedicated microSD** for XGB35 using local build artifacts. It places the Actions BootROM payload before partition 1 and installs an Alpine filesystem and 480×320 stock userspace images on that partition. The local XGB35 boot build supplies the runtime and graphical menu.

## Requirements

- Windows, Python 3.11 or newer, and an existing WinUSB binding for the XGB35 ADB gadget `10d6:fe02`.
- A working XGB35 RAM boot with ADB and `/bin/xgbshell`.
- A spare microSD in the console. Installation replaces the MBR and formats the card.
- These local artifacts from the XGB35 build. The installer release contains the host tools, while the XGB35 CFW payload, Alpine bundle and stock images remain local inputs.

```text
build-artifacts/
  sd-boot-autonomous/
    manifest.json
    sd-boot-lba1.bin
  alpine-bundle/
    manifest.json
    alpine-xgb35-rootfs.tar.gz
  update.zip_DATA/
    3.system.img
    5.res.img
```

The SYSTEM and RES inputs are the **480×320 userspace images** used with the XGB35 runtime. The August 2023 stock recovery release contains a different display profile. The installer checks the two expected SHA-256 digests before connecting to the console.

## Verify and install

From the extracted installer folder, verify the inputs without USB access:

```powershell
python install_sd.py --inputs 'D:\path\to\build-artifacts'
```

Once the console is in its XGB35 RAM session and the spare card is inserted, explicitly request a card erase and installation:

```powershell
python install_sd.py --inputs 'D:\path\to\build-artifacts' --erase-sd
```

The tool checks that Linux identifies `/dev/mmcblk0` as an SD card, provisions the boot sectors and mounts a new partition 1 starting at LBA 32768. It installs `/linux/alpine.ext4`, `/linux/stock-system.img` and `/linux/stock-res.img`, checks the boot area and stock images by readback, and unmounts the partition. The host installer targets the microSD device.

**Verification status:** the input validation and ADB protocol self-test passed on Windows. The installation and autonomous cold boot still need hardware validation. The standalone CFW is being finished separately.
