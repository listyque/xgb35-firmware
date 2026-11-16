# Stock XGB35 firmware · August 22, 2023

**EN** · [RU](README.ru.md)

- **Download:** [`update.zip` on GitHub Releases](https://github.com/listyque/xgb35-firmware/releases/download/stock-2023-08-22/update.zip)
- **Size:** 72,764,416 bytes
- **SHA-256:** `957ab55ae5c8af8994f47b4b403d64808c59b2bb01cc9af7f443302c761516e2`

This **Actions PATO OTA** file was reconstructed from older firmware components read from the tested XGB35 NAND. The container version `2.0.00.230822.0628` was derived from the original build date and time. The original 2023 OTA header was unavailable. The included payloads all come from the older NAND backup.

| PATO entry | Content |
| --- | --- |
| U-Boot | Older bootloader extracted from NAND. The updater targets the boot area |
| MISC | Older kernel, device tree, ramdisk and boot settings |
| RECOVERY | Older recovery filesystem and stock updater |
| SYSTEM | Older SquashFS operating system and stock `manager` |
| RES | Older SquashFS interface resources |

Offline checks covered the PATO signature, entry bounds, header and payload checksums, both U-Boot CRCs and the filesystems. **The reconstructed OTA has not been installed on the console.** The package updates the boot area and the older MISC selects a **1024×600** profile. Compatibility with the tested 320×480 panel still needs verification. CONFIG, UPGRADE and the user GAME partition are outside this package.

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath .\update.zip
```
