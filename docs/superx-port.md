# SuperX CFW V1.1 port to XGB35

**EN** · [RU](superx-port.ru.md)

**Status:** offline compatibility review. V1.1 lists **Powkiddy X39, X39 Pro, X45, X51 and X70**. XGB35 boot and gameplay under SuperX remain unverified. [Upstream project](https://github.com/scooper540/powkiddy-X39-X45-X51-X70-cfw) · [V1.1 release](https://github.com/scooper540/powkiddy-X39-X45-X51-X70-cfw/releases/tag/V1.1)

## Required changes

| Area | Upstream behavior and XGB35 change |
| --- | --- |
| Boot and storage | Upstream `run.sh` expects the stock host to mount the card at `/mnt/card`, exposes `/mnt/original` and calls `/etc/startup.sh` in `rootfs.ext3`. Boot the XGB35 kernel and device tree first. Use an XGB35 launcher that loop-mounts `rootfs.ext3`, exposes the card at `/mnt/card`, binds `/proc`, `/sys`, `/dev`, and starts the frontend with the XGB35 environment. Preserve ADB for logs. The upstream `update.zip` contains a PATO `upgrade.img` entry targeting UPGRADE, so leave it and upstream `run.sh` off the test card root |
| ARM userspace | `simplermenu_plus`, RetroArch and `power_volume_handler` are ARM hard-float ELF binaries that request `/lib/ld-linux-armhf.so.3`. Keep the upstream rootfs and its runtime loader inside the chroot. Confirm the binaries start under the XGB35 ARMv7 kernel before debugging video |
| LCD and rotation | Keep the working XGB35 320×480 RGB565 framebuffer and panel initialization. Give the frontend a 480×320 landscape canvas with `SDL_VIDEODRIVER=fbcon`, `SDL_VIDEO_FBCON_ROTATION=CW`, `SCREEN_WIDTH=480` and `SCREEN_HEIGHT=320`. Set SimplerMenu Plus `screenWidth=480` and `screenHeight=320` and supply a **`GENERAL_480320`** layout for the selected theme. Start RetroArch with `video_rotation = "1"` and check an asymmetric test image in both programs for a double rotation |
| RetroArch video | Upstream uses `sdl_powkiddy` and its Actions display-engine upscaler. Validate RGB565, stride, pan and scaling on XGB35. If the driver depends on a different kernel interface, use a software SDL/framebuffer path available in the build or rebuild RetroArch with one. Verify a single lightweight core before testing hardware scaling |
| Buttons | Reload the XGB35-compatible `owl_gpio_matrix_adcjoystick.ko` for the running 3.10.52 kernel. The local launcher attempts `tiny_mode=0` with the framebuffer blanked during the switch. Capture the actual `/dev/input/event*` events and correct `KEY_*`, `BTN_*`, axis mappings and hotkeys in SimplerMenu Plus and RetroArch |
| Audio, brightness and power | Bring up stock XGB35 ALSA and backlight controls before invoking SuperX audio and power helpers. Confirm speaker, headphones, brightness, volume and shutdown separately. Upstream mixes hard-coded `tinymix` indices and writes codec registers. Mask those paths until the XGB35 mixer map has been verified |
| Clock and USB | Keep the working XGB35 clock settings during first boot and keep ADB online. The prepared theme and RetroArch artifacts still leave SimplerMenu Plus configured to select **1008 MHz** even though the local launcher attempts a **900 MHz** cap. Remove this override or restrict the menu to board-verified frequencies. Leave Wi-Fi disabled during first bring-up and validate USB host mode separately |

## What the local prototype already does

The local preparation script selects 480×320, adds an Epic Noir 480×320 layout and changes RetroArch rotation to 1. The XGB35 runtime already has a SuperX menu entry and a chroot launcher. The files confirm those changes exist. A successful boot, input map, audio path and hardware upscaler are still open tests.

For the first hardware run, put `rootfs.ext3` and `cfw/` on a spare XGB35 microSD. Select SuperX from the XGB35 RAM-booted menu. Capture framebuffer geometry and process logs over ADB, verify controls with one core, and test sound and reset before attempting independent microSD boot. ROMs and BIOS files are outside this port.
