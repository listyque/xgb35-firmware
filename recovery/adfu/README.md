# XGB35 brick recovery over USB ADFU

[Русская инструкция](README.ru.md)

[Download the Windows x64 recovery kit](https://github.com/listyque/xgb35-firmware/releases/tag/adfu-recovery-v0.1.0) with the driver, image, portable Python and `START.cmd`. The illustrated beginner guide is `START-HERE.ru.html`.

This kit targets the XGB35 with **LLW7051-057-MAIN-BOARD V2.0 (2022-07-19)** and Micron **MT29F64G08CBABA WP:B**, ID `2c 64 44 4b a9`. A physical page contains 8192 data bytes and 744 OOB bytes. Match the photos and NAND before using a board-specific image. [Board details](../../docs/hardware.md) · [ADFU pads](../../photos/adfu-close.png)

1. Remove the microSD with the console powered off. Connect the data USB port to Windows. Try Reset first. On the tested unit, a failure to find a bootable NAND sector could enter BootROM ADFU automatically. If needed, bridge the two verified ADFU pads shown in the photo during Reset and remove the bridge after USB enumeration. The device is `USB\VID_10D6&PID_10D6`. ADB in a running Linux session has a different USB ID.
2. In an elevated PowerShell, install the included Actions WinUSB driver if Windows has not bound it. Then list its device interfaces and copy the complete path for `VID_10D6&PID_10D6`, starting with `\\?\USB#`.

   ```powershell
   pnputil /add-driver .\adfu-driver\adfuwinusb.inf /install
   pnputil /enum-interfaces /enabled /class '{FF7B846F-B0A3-45C0-B0FC-D8122086996D}'
   $env:ACTIONS_ADFU_PNP_PATH = Read-Host 'Paste the complete ADFU interface path'
   ```

3. Start with a raw backup of the two boot blocks. This reads the NAND and writes a file on the PC. Keep a copy and record its SHA-256 before continuing.

   ```powershell
   python -B adfu_read_nand.py $env:ACTIONS_ADFU_PNP_PATH --pages 0 512
   Get-FileHash -Algorithm SHA256 .\adfu-nand-pages-0-512-raw-with-oob.bin
   ```

   Expect 4,575,232 bytes with OOB. Save that file before repeating the read and compare the two results. For the first 32 MiB of NAND data and its OOB, omit `--pages`.
4. Verify the diagnostic RAM service and the write plan offline. Then start the service from BootROM ADFU and capture the live block 0 using `inspect`. The stock recovery archive is a separate PATO OTA package for the stock updater.

   ```powershell
   python -B adfu_autonomous_service.py --dry-run --diagnostic
   python -B adfu_write_nand_block0.py plan
   python -B adfu_autonomous_service.py --diagnostic
   python -B adfu_write_nand_block0.py inspect
   ```

5. Use `rollback` only when restoring the verified block-0 image for the matching board and NAND. Substitute both digests from `inspect`. This erases physical block 0, programs it with OOB, and verifies the result. It preserves block 1. `write` selects an experimental target and is separate from the recovery procedure.

   ```powershell
   python -B adfu_write_nand_block0.py rollback --expect-current '<current_block0_sha256>' --execute-block0 '<target_hashes.rollback>'
   ```

   Wait for `complete` before resetting. If a write stops, preserve the journal and the original snapshot. Enter ADFU again and run `inspect` to establish the current state. On the test unit, raw readback differed on row 4. Recovery completed after resuming and checking the decoded data with hardware ECC. `python -B adfu_resume_block0.py --start-row 5` checks the known partial layout without programming. The script also recognizes resume points 9 and 12 for that layout and rejects a resume when pending rows already contain data. Programming with `--continue-block0` requires the exact rollback image SHA-256.

[Recovery archive and SHA-256](nand-write-preparation/) · [BootROM NAND notes](atm7051h-bootrom-nand.md) · [Stock PATO OTA](../../firmware/stock/README.md)

The published plan and both RAM service builds can be verified without a device. The block-0 archive is from the tested unit. A black screen, a different NAND ID, or a damaged secondary boot block needs separate diagnosis.
