# Каталог игровых портов (XGB35 / ATM7051H Linux)

Единый каталог нативных портов игр для приставки **XGB35** (**Actions ATM7051H**, `armv7` Cortex-A5, Alpine Linux `musl`).
Каждый порт поддерживает работу в двух режимах:
1. **HDMI Linux Desktop (X11)** — оконный и полноэкранный (`F11`) запуск в среде `/desktop` (720p / 1080p) с полноценным управлением **мышью и клавиатурой**.
2. **Портативный режим на экране 3,5″ (`480×320`, `/dev/fb0`)** — прямой вывод с аппаратным поворотом ST7796S, управлением со встроенных аналоговых стиков/кнопок **и** поддержкой внешних USB мыши/клавиатуры через `evdev`.

## Реестр портов

| № | Игра / Порт | Папка | Версия / Движок | Режимы экрана | Управление | Статус |
| :-: | --- | --- | --- | --- | --- | --- |
| 1 | **[StarDroid](stardroid/README.md)** *(StarCraft & Brood War)* | [`ports/stardroid/`](stardroid/) | [kinopiowarrior/StarDroid](https://github.com/kinopiowarrior/StarDroid) `v1.9.11` (OpenBW C++17) | X11 Desktop (`640×480`..`1080p`) + LCD `480×320` (`Full` и `1:1 Zoom`) | USB мышь + клавиатура, стики + геймпад XGB35 | Готов (`dist/bin/stardroid`) |
| 2 | **[Jellyfin FPL](Jellyfin_fpl/README.md)** *(Jellyfin Folder Player)* | [`ports/Jellyfin_fpl/`](Jellyfin_fpl/) | Форк [streamyfin/streamyfin](https://github.com/streamyfin/streamyfin) (C++17 + FFmpeg + cURL + FreeType) | X11 Desktop (`854×480`..`1080p`) + LCD `480×320` (`1:1 RGB565`) | Геймпад XGB35 + USB мышь/клавиатура + экранная клавиатура RU/EN | Готов (`dist/bin/jellyfin_fpl`) |
| 3 | **[OpenGorky](opengorky/README.md)** *(Gorky 17 / «Горький-18»)* | [`ports/opengorky/`](opengorky/) | OpenGorky (Zig + нативный XGB35 C99/NEON бэкенд, без SDL2) | X11 Desktop + `DE_ML0` 0 мс скейлер (`720p`/`1080p`) + LCD `480×320` (`Full` / `1:1 Zoom`) | Геймпад XGB35 + стики + USB мышь/клавиатура | Готов (`dist/bin/opengorky`) |

## Авторы оригинальных проектов

| Порт | Авторы и лицензии |
| --- | --- |
| StarDroid | **ToadWarrior** (`kinopiowarrior`) и авторы **OpenBW**, включая участников ветки **heiner/openbw**. Указание авторов библиотек и материалов сохранено отдельно. [Релиз 1.9.11](https://github.com/kinopiowarrior/StarDroid/releases/tag/v1.9.11) · [Участники OpenBW](https://github.com/OpenBW/openbw/graphs/contributors) · [Уведомления StarDroid](stardroid/THIRD_PARTY.md) |
| Jellyfin FPL | **Fredrik Burmester** и участники **Streamyfin**. Исходные сведения об авторах и лицензия сохранены в `upstream/`. [Авторы Streamyfin](Jellyfin_fpl/upstream/README.md#-credits) · [Участники](https://github.com/streamyfin/streamyfin/graphs/contributors) · [Лицензия](Jellyfin_fpl/upstream/LICENSE.txt) |
| OpenGorky | **m1ome** и участники **OpenGorky**. Оригинальная игра принадлежит её авторам **Metropolis Software**. [Участники OpenGorky](https://github.com/m1ome/opengorky/graphs/contributors) · [Лицензия движка](opengorky/upstream/LICENSE) |
| Open MainUI / OnionOS | **robcodedev** и участники **Open MainUI**, **Onion Team** и участники **OnionOS**. [Участники Open MainUI](https://github.com/robcodedev/onionos-mainui-opensource/graphs/contributors) · [Команда Onion](https://onionui.github.io/about) · [Исходники и уведомления](onion-mainui/) |
| SuperX | **scooper540** и участники SuperX CFW. [Участники проекта](https://github.com/scooper540/powkiddy-X39-X45-X51-X70-cfw/graphs/contributors) · [Адаптация для XGB35](../docs/superx-port.ru.md) |

Для нового порта указывайте исходный репозиторий и версию, авторов и ссылку на список участников, лицензию, изменения XGB35 и способ сборки. Для StarDroid и OpenGorky понадобятся игровые данные из собственной копии игры.

## Структура папки каждого порта

```text
ports/
  README.md                 # Этот каталог всех портов
  <game-slug>/
    README.md               # Описание порта, файлов данных и управления
    build_port.py           # Воспроизводимая кросс-компиляция под ARMv7 musl
    deploy_adb.py           # Установка на приставку в 1 команду по USB ADB
    src/                    # Код адаптера/фронтенда под Linux Desktop + XGB35
    upstream/               # Исходники оригинального проекта
    dist/                   # Готовый пакет для копирования в /mnt/sd/ports/<game-slug>/
```
