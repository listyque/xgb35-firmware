# XGB35 Linux builds

| Variant | Build and installation |
| --- | --- |
| XGB35 RAM Linux and diagnostics | [USB recovery tools](../../recovery/adfu/), [kernel, DTB and initramfs](../../recovery/adfu/usb-storage-ram/) and [RAM service](../../recovery/adfu/autonomous-service/) |
| XGB35 microSD multiboot | [Windows installer](../../multiboot/), [SD boot sectors and manifest](../../recovery/adfu/sd-boot-autonomous/), [Alpine bundle](../../recovery/adfu/alpine-bundle/) and [480×320 stock userspace](../../recovery/adfu/update.zip_DATA/) |
| Alpine Desktop / HDMI | [Desktop root filesystem archive](alpine-desktop-rootfs.tar.gz), [runtime source](../../recovery/adfu/adfu_usb_storage_ram.py) and [PowerVR SGX540 source and build](../../drivers/sgx540/) |
| OnionOS / Open MainUI | [Source and credits](../../ports/onion-mainui/), [XGB35 SD bundle](../../recovery/adfu/onionos-bundle/) and [build script](../../recovery/adfu/build_onion_xgb35.py) |
| SuperX V1.1 | [Experimental XGB35 SD bundle](superx-xgb35-sd.tar.gz), [upstream release and XGB35 port](../../docs/superx-port.md), [SD preparation](../../recovery/adfu/prepare_superx.py) |
| X70 RAM reference | [Kernel, DTB and initramfs](../../recovery/adfu/x70-recovery-ram/), [loader](../../recovery/adfu/adfu_x70_recovery_ram.py) |
| Reconstructed stock firmware | [August 2023 OTA and compatibility notes](../stock/README.md) |
| Early ATM7051H diagnostic Linux | [Source and package](../diagnostic/) |

Each variant has its own boot method. Use the manifest beside the build to identify its exact assets.

For a Git clone, run `git lfs pull` to download the Desktop and SuperX archives. The Desktop archive contains the unpacked filesystem with Linux permissions. The runtime expects an ext2 image at `/linux/desktop.ext2` on the microSD. On Linux, unpack the archive as root into an empty directory, then build the image with `mke2fs -t ext2 -F -d <root-directory> desktop.ext2 320M`. The source image contained the tested Desktop setup. The repacked archive was checked offline.
