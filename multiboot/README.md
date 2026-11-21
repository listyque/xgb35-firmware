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

## PowerVR SGX validation

The rebuilt CFW includes `/bin/sgx-init` for the Actions 3.10.52 kernel. It mounts `/mnt/sd/linux/stock-system.img` read-only and runs the stock `pvrsrvctl` with the uClibc libraries from the same image. On an existing CFW boot, run these commands from the `multiboot` directory:

```powershell
python adb_winusb.py push sgx-init.sh /tmp/sgx-init.sh
python adb_winusb.py shell '/bin/busybox sh /tmp/sgx-init.sh'
python adb_winusb.py shell 'cat /proc/pvr/version; dmesg | grep -i "DDK"'
```

On the XGB35 at 900 MHz, `/dev/pvrsrvkm` appeared and the kernel confirmed matching DDK builds `1.14@4004414`. The driver reported revision `130`. The script is run manually at the current CPU frequency.

The stock 1.14 DDK requires compatible ARMv7 EGL/GLES libraries and a window-system backend. The inspected SYSTEM and RES images did not contain that stack. After stock PowerVR Services initialized, `glxinfo -B` in Alpine Desktop reported `llvmpipe` and `Accelerated: no`. The hardware probe uses the separate 1.8 DDK stack described below.

- The 2022-11-23 Powkiddy X70 SYSTEM image uses DDK build `4004414`. Its `pvrsrvctl`, `libsrv_init.so`, `libsrv_um.so`, and `libpvr2d.so` are byte-identical to those on the XGB35. Neither SYSTEM nor RES contains EGL, GLES, or WSEGL. [X70 firmware archive](https://fox-exe.ru/Files/PowKiddy_x70/)
- The LeMaker Guitar module targets `3.10.37` with DDK `MAIN@3625561`. The board manual identifies its GPU as SGX544MP. [LeMaker module](https://github.com/elar-systems/LeMaker-Guitar/blob/master/hwpack/rootfs/lib/modules/3.10.37/kernel/drivers/gpu/pvr/eurasia_km/pvrsrvkm.ko) · [Manual](https://mirror.lemaker.org/LeMaker_Guitar%E7%94%A8%E6%88%B7%E6%89%8B%E5%86%8CV1.0.pdf)
- The CI20 SGX540 rev. 130 package uses MIPS and DDK `1.14.3841212`. The CI20 research code opens the DRM device and creates a device memory context. Its rendering test calls proprietary GL. [DDK versions](https://github.com/openpvrsgx-devgroup/linux_openpvrsgx/wiki/DDK-versions) · [CI20 source](https://github.com/Hazematman/sgx540-reversing)
- The LeMaker Linux Actions `3.10.y` tree has OWL platform code in DDK `1.12@2701748`. Its `gs702c` build selects SGX540 rev. 130 and `SUPPORT_SLC`. This provides a reference for the port. The stock XGB35 module uses platform resources, runtime PM and the DTB's OPP table. [OWL platform code](https://github.com/LeMaker/linux-actions/tree/linux-3.10.y/drivers/gpu/drm/gpu/eurasia_km/services4/system/owl) · [Build configuration](https://github.com/LeMaker/linux-actions/blob/linux-3.10.y/drivers/gpu/drm/gpu/eurasia_km/eurasiacon/build/linux2/owl_android/Makefile)

The archived DDK `1.8@869593` builds EGL, GLES2, the GLSL compiler and Services for ARMv7 Cortex-A5, SGX540 rev. 130 and glibc. QEMU loaded `libEGL.so`, `libGLESv2.so` and `libglslcompiler_SGX540_130.so` and ran `pvrsrvctl --help`. The stock `1.14@4004414` module uses a different interface. The packaged Alpine build uses musl. [DDK source archive](https://github.com/aosp/gfx_linux_ddk)

The stock DTB node `actions,ats3605-gpu` describes the register range at `0xb0350000` with size `0x10000`, GIC SPI 4, four clocks named `gpu`, `gpu_core`, `gpu_mem` and `gpu_sys`, the `gpu` reset and a power domain. The defined GPU operating points are 218, 327 and 436 MHz. The 900 MHz test setting refers to the CPU. Revision `130` comes from the driver's report. The hardware core ID remains unverified.

The DDK 1.8 patch attaches to ATS3605 through device tree and handles GPU power, clocks and interrupts. The reference Linux 3.10.52 build succeeded from the original archive. The module and stock driver have matching `vermagic`, `struct module` size, and `init` and `exit` offsets. The stock kernel exports all 140 symbols imported by the diagnostic build. The reference build uses a VExpress kernel configuration. [Build script](../drivers/sgx540/build-reference.sh) · [Patch](../drivers/sgx540/ddk18-ats3605.patch)

The probe creates an EGL pbuffer, draws a green triangle on black, and checks the center and corner colors. The default frame size is 32×32. Running `gles2-probe triangle.ppm` draws a 256×256 frame and saves the pixels from `glReadPixels` for viewing. The WSEGL configuration self-test also passed in QEMU. [GLES2 probe](../drivers/sgx540/gles2-probe.c)

ADB commands stopped responding after running `pvrsrvctl --dump-debug --force-regs` on the test unit. Read `/proc/pvr/version` to check the driver version.

In Desktop Mode on the XGB35 at 900 MHz, `es2_info` reported `GL_RENDERER: llvmpipe` and `OpenGL ES 3.2 Mesa 23.3.6`. The DDK 1.8 module loaded from RAM, bound to the GPU and created `/dev/pvrsrvkm`. During Services startup, the microkernel left its ready bit clear. `PVRSRVInitSrvDisconnect` returned `24` (`PVRSRV_ERROR_RETRY`) and the render probe did not start. ADB stopped responding on the first attempt. The diagnostic build turned off the GPU clocks and power after the error. ADB and Xorg responded after the second test, and the test module was absent.

After enabling the SGX system cache, which the stock driver configures through `MNE_CR_CTRL`, `pvrsrvctl --start --no-module` succeeded. The native port skips the Android `/sys/egl/egl.cfg` setup that prevented reloading in the same session.

The probe sets `ParamBufferSize=262144` and `MaxParamBufferSize=262144` in the local `powervr.ini`. The module uses the exported kernel page allocator because the stock `node_zonelists` offset (`0xb40`) differs from VExpress (`0x600`). With automatic idle power-off enabled, the microkernel timed out restarting and `WaitForRender` timed out.

With automatic idle power-off disabled, the probe passed twice on separate module loads from RAM. On the XGB35 in Desktop Mode with the CPU at 900 MHz, the driver reported `PowerVR SGX 540` and a cached 120 MHz GPU clock reading. Firmware programs DEVPLL outside Linux clock accounting. The center was green `(0,255,0)`, the corner was black `(0,0,0)`, and `glGetError` returned zero. A third run captured a 256×256 frame with the same sample colors. Stock Alpine applications continue to use Mesa.

The stock `imlib2_view` displayed the saved frame on `:0`. A screenshot of that window using `imlib2_grab -id` matched `glReadPixels` at all 65,536 pixels, including 20,808 green pixels inside the triangle. This presentation path copies the completed frame into X11.

### GLES2 benchmark on the XGB35

The same source draws 15 rotating, lit checkerboard cubes with perspective and depth testing, for 180 triangles per frame. Both renderers use a 480×320 RGBA8 pbuffer with 24-bit depth, 8-bit stencil and no MSAA. After a one-second warm-up, each of three runs renders for five seconds. A synchronous one-pixel read completes each frame. X11 presentation and saving the reference image take place outside the timed interval. [Benchmark source](../drivers/sgx540/gles2-cubes.c)

| Renderer at 900 MHz CPU | Frames in three runs | Median FPS |
|---|---|---:|
| PowerVR SGX540, inherited GPU clock (unverified) | 1547 / 1545 / 1547 | 309.2 |
| Stock Mesa llvmpipe | 76 / 76 / 75 | 15.1 |

The ratio of median frame rates was 20.5 on this scene. The benchmark takes the expected renderer substring as its first argument and an optional reference PPM path as its second. On Alpine Desktop, set `EGL_PLATFORM=surfaceless` to use RGBA8. One PowerVR microkernel startup timed out before measurement. Reloading the module succeeded in the same boot.

### Alpine Desktop driver

The release contains a matching Linux 3.10.52 module, Services, shader compiler and musl EGL/GLES2 libraries. Build both halves in the same working directory with the inputs and checksums specified in the scripts. The kernel script uses an isolated Ubuntu build environment under /opt. The userspace build uses the ARM hard-float cross compiler, 32-bit host C/C++ toolchain and qemu-arm. [Kernel build](../drivers/sgx540/build-reference.sh) · [Alpine build](../drivers/sgx540/build-musl.sh)

```sh
sh "$DRIVER/build-reference.sh"
sh "$DRIVER/build-musl.sh"
python3 "$DRIVER/package-driver.py" pvrsrvkm_sgx540_130-reference.ko ddk18/um-musl/target xgb35-sgx540-alpine.tar.gz
```

Copy the archive and install-driver.sh to the Desktop filesystem. Run the following inside Alpine Desktop as root. The program and its arguments follow sgx-run. Each application opts in separately. The backend supports EGL pbuffers. OpenGorky composites the menu result into its RGB565 framebuffer. [Installer](../drivers/sgx540/install-driver.sh)

```sh
sh install-driver.sh xgb35-sgx540-alpine.tar.gz
sgx-run --init
sgx-run ./cubes 'PowerVR SGX 540'
sgx-run --stop
```

The packaged module defaults to 360 MHz on all three GPU clocks, derived from the 720 MHz DDRPLL through the GPU dividers. The 120 MHz test profile uses the same parent. The CPU policy stays under OS control. On the 480×320 cube test, the median of three five-second trials was 167.5 FPS at CPU/GPU 900/120 MHz and 264.3 FPS at 1200/360 MHz. The register readbacks were 0xFE360036 and 0xFA320032. A request for 400 MHz on the original clock tree failed. The earlier 309.2 FPS run used an inherited clock whose hardware divider was not captured.

#### Direct fbdev output

`sgx-run --fbdev PROGRAM [ARGS...]` selects the fbdev WSEGL module. Use `EGL_DEFAULT_DISPLAY`, request an RGB565 config with `EGL_WINDOW_BIT`, and pass `(EGLNativeWindowType)(uintptr_t)1` for `/dev/fb0` or `2` for `/dev/fb1`. The backend uses the native framebuffer orientation and requires two complete RGB565 pages and a display owned by the application.

Services imports the framebuffer mapping into SGX device memory. EGL waits for rendering before WSEGL pans to the back page with `FBIOPAN_DISPLAY` and `FB_ACTIVATE_VBL`. The probe verified five flips on each framebuffer and restoration of the initial page. The presentation path has zero pixel readbacks. [WSEGL implementation](../drivers/sgx540/wsegl-fbdev.c) · [GLES2 probe](../drivers/sgx540/gles2-fbdev-probe.c)
