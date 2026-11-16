# XGB35 (ATM7051H) · Stock Firmware, CFW and Multiboot

**EN** · [RU](README.ru.md)

![XGB35 handheld game console with a 3.5-inch display](https://imgaz3.staticbg.com/thumb/large/oaupload/banggood/images/7E/63/02f22f34-783a-42d9-942a-22dad2963375.jpg)

XGB35 catalog photo from Banggood. [Product listing and photo credit](https://www.lowcychin.pl/retrokonsola-xgb35/)

The **XGB35** is a handheld game console based on the Actions Semiconductor **ATM7051H**. This repository documents a unit with board **LLW7051-057-MAIN-BOARD V2.0** dated **2022-07-19**, an **XXL03518B** display flex cable and an **8 GiB Micron NAND**. Several sellers use the XGB35 name. Match the board and panel markings before choosing a firmware image.

| Section | Content |
| --- | --- |
| [Hardware and photos](docs/hardware.md) | Board, NAND, memory, display, power, boot, USB and debug interfaces |
| [Stock firmware](firmware/stock/README.md) | Actions PATO recovery package rebuilt from the August 2023 NAND components |
| [XGB35 Multiboot](multiboot/README.md) | Windows installer preview for provisioning a microSD card from local build artifacts |
| [SuperX CFW port](docs/superx-port.md) | Compatibility findings and required changes for XGB35 |
| [Firmware status](docs/firmware.md) | XGB35 CFW, multiboot and SuperX status |

## SuperX CFW for Powkiddy

**SuperX CFW V1.1** by **scooper540** supports the **Powkiddy X39, X39 Pro, X45, X51 and X70**. The XGB35 port is experimental. The linked analysis identifies changes in boot integration, display orientation, input, audio and clock settings. [Upstream source](https://github.com/scooper540/powkiddy-X39-X45-X51-X70-cfw) · [SuperX V1.1](https://github.com/scooper540/powkiddy-X39-X45-X51-X70-cfw/releases/tag/V1.1)
