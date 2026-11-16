# Firmware and multiboot status

| Project | Status |
| --- | --- |
| [Reconstructed stock firmware](../firmware/stock/README.md) | August 2023 PATO package verified offline. Installation with this package has not been tested on the console |
| XGB35 CFW | Linux booted from RAM, with WinUSB ADB and a framebuffer terminal. The standalone CFW build is still in development |
| [XGB35 Multiboot installer preview](../multiboot/README.md) | Provisions microSD from local boot, Alpine and stock assets. The boot payload contains the in-development XGB35 CFW and is supplied by the operator. Cold boot from the card remains unverified |
| [SuperX CFW for Powkiddy](superx-port.md) | Upstream V1.1 supports X39, X39 Pro, X45, X51 and X70. The XGB35 port plan is based on the upstream release and local artifacts |

Future releases should identify the board revision, boot medium, exact asset hash and the hardware tests that passed.
