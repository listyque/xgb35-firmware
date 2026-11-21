# XGB35 (ATM7051H) · Штатная прошивка, CFW и мультибут

[EN](README.md) · **RU**

![Портативная игровая консоль XGB35 с экраном 3,5 дюйма](https://imgaz3.staticbg.com/thumb/large/oaupload/banggood/images/7E/63/02f22f34-783a-42d9-942a-22dad2963375.jpg)

Каталожное фото XGB35 от Banggood. [Страница с фото и описанием модели](https://www.lowcychin.pl/retrokonsola-xgb35/)

**XGB35** — портативная игровая консоль на Actions Semiconductor **ATM7051H**. Здесь описан экземпляр с платой **LLW7051-057-MAIN-BOARD V2.0** от **2022-07-19**, LCD со шлейфом **XXL03518B** и внутренней NAND Micron на **8 ГиБ**. Название XGB35 встречается у нескольких продавцов. При выборе прошивки сверяйте маркировку платы и панели.

| Раздел | Содержание |
| --- | --- |
| [Железо и фотографии](docs/hardware.ru.md) | Плата, NAND, память, экран, питание, загрузка, USB и диагностические интерфейсы |
| [Штатная прошивка](firmware/stock/README.ru.md) | Пакет восстановления Actions PATO из разделов NAND со сборкой от 22 августа 2023 года |
| [Восстановление «кирпича» через ADFU](recovery/adfu/README.ru.md) | Драйвер Windows, резервная копия NAND, диагностика из RAM и восстановление блока 0 |
| [Сборки Linux](firmware/linux/README.md) | Загрузка из RAM, мультибут с microSD, Alpine Desktop, OnionOS и SuperX |
| [Мультибут XGB35](multiboot/README.ru.md) | Предварительная версия установщика для Windows. Готовит microSD из локальных файлов сборки |
| [Порт SuperX CFW](docs/superx-port.ru.md) | Совместимость и изменения, необходимые для запуска на XGB35 |
| [Статус прошивок](docs/firmware.ru.md) | Собственная CFW XGB35, мультибут и SuperX |
| [Порты и авторы](ports/README.md) | Приложения, игры и оболочки для XGB35 |

## SuperX CFW от Powkiddy

**SuperX CFW V1.1** от **scooper540** поддерживает **Powkiddy X39, X39 Pro, X45, X51 и X70**. Порт для XGB35 экспериментальный. В обзоре описаны необходимые изменения загрузки, ориентации экрана, управления, звука и частот процессора. [Репозиторий SuperX CFW](https://github.com/scooper540/powkiddy-X39-X45-X51-X70-cfw) · [Релиз V1.1](https://github.com/scooper540/powkiddy-X39-X45-X51-X70-cfw/releases/tag/V1.1)
