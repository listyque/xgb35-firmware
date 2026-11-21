# StarDroid 1.9.11 — Authentic StarCraft & Brood War Port (XGB35 Linux Desktop & Handheld)

Полный нативный порт **StarDroid 1.9.11** (OpenBW C++ simulation + оригинальный интерфейс Blizzard StarCraft / Brood War с адаптациями под приставку) для **ATM7051H / XGB35**:
- **Linux Desktop (HDMI / X11)**: оконный режим и полноэкранный режим `F11` (`16bpp RGB565` и `24/32bpp BGRX`), поддержка USB-мыши и USB-клавиатуры.
- **Маленький экран приставки (`480×320 /dev/fb0`)**: прямой рендер на `ST7796S` с переключением `L2 / F2` между полным экраном `640×480` и чётким режимом `1:1 (480×320)` со слежением за курсором.
- **Звук и видео**: прямой вывод `ALSA S32_LE 22050 Hz` (HDMI `pcmC1D0p` или динамик `pcmC0D0p`), оригинальная музыка `.wav`, озвучка брифингов и кампаний, анимированные `.smk`-планеты меню, говорящие портреты и потоковые полноэкранные видеоролики `.smk` (`< 2 МБ` RAM).

## Оригинальный интерфейс StarDroid / Blizzard
- **Заставка (`glue\title\title.pcx`)** и **Главное меню (`rez\glumain.bin` + `palmm`)** с вращающимися `.smk`-планетами (*Один игрок*, *Своя игра*, *Настройки*, *Вступление*, *Авторы*, *Выход*).
- **Выбор версии (`rez\glugamemode.bin` + `retail_ex.pcx`)**: оригинальный *StarCraft* или *Brood War*.
- **Выбор кампании (`glucmpgn` / `gluexpcmpgn`)** со статуями рас и окном истории миссий **`gluhist` (`phist.pcx`)** (отметки пройденных миссий, встроенные межмиссионные ролики `.smk`, опция разблокировки всех миссий).
- **Текстовые заставки эпизодов (`rez\est*.txt`)** и **Комната брифинга (`glurdy{t,z,p}.bin`)**: исполнение триггеров `CHK MBRF`, 4 анимированных `.smk`-окна говорящих персонажей с рамками и масками, синхронная с речью прокрутка текста, цели миссии, *Start / Replay / Cancel*.
- **Своя игра (`glucustm.bin`)** с живым рендером `128×128` превью ландшафта и стартовых точек из `CHK`, выбором режима (`Melee`, `FFA`, `Teams`, `Use Map Settings`), сложности, скорости, опций StarDroid (`Auto-Harvest`, `Multi-Larva`, `Smart-Cast`, `Hero Mode`) и таблицей 8 слотов игроков/ИИ.
- **Загрузка сохранений и повторов (`gluload.bin`)**.
- **Родная игровая консоль расы (`game\{z,t,p}console.pcx`)**: живой говорящий `.smk`-портрет юнита/советника/сюжетных передач, миникарта `128×128`, кнопки `MENU` и ландшафта (`Tab`), ресурсы `icons.grp`, каркасы выделения и десанта, очередь производства `statdata.bin`, карточка приказов `3×3` (`cmdicons.grp`), классическое меню `F10` (`gamemenu.bin`, советы `tips.tbl`, задачи миссии, сохранения, читы) и послематчевый **экран счёта (`gluscore.bin` + `iscore.grp`)**.
- Автоматическая поддержка **русских (`CP1251`)** и английских архивов `MPQ` (включая расшифровку шрифтов `ClassicFontCipher` SHA-0 + IDEA).

## Управление на приставке и с мыши/клавиатуры
- **Левый стик / USB-мышь** — перемещение курсора (удержание `L1` замедляет курсор для точного наведения).
- **Правый стик / Крестовина (D-Pad)** — прокрутка камеры на карте (в меню крестовина переключает кнопки).
- **`A` / ЛКМ** — выбор, выделение рамкой (удержание `A` + левый стик), двойное нажатие `A` — выбор всех юнитов того же типа на экране.
- **`B` / ПКМ** — умный приказ (движение / сбор ресурсов / атака) или отмена режима постройки/цели; в меню — Назад.
- **`X`** — Атака с перемещением (`Attack-Move`).
- **`Y`** — Постройка (`Build`) для рабочих / Стоп (`Stop`) для войск.
- **`L1` / `R1`** — вызов контрольных групп `1` / `2` (`R2 + L1/R1` — назначить группу; `R2` также работает как `Shift`).
- **`L2` / `F2`** — переключение экрана `640×480` <-> `1:1 (480×320)` на 3.5″ дисплее.
- **`SELECT`** — переход камеры к последнему событию (`Space`), **`START` / `F10`** — игровое меню (`SELECT + START` — быстрый выход).

## Сборка и обновление на приставке
```powershell
python ports/stardroid/build_port.py
python ports/stardroid/deploy_adb.py
```
# Upstream source for building

Extract the upstream [StarDroid 1.9.11 developer source ZIP](https://github.com/kinopiowarrior/StarDroid/releases/download/v1.9.11/StarDroid-1.9.11-source.zip) into `ports/stardroid/upstream/` so that `upstream/native/runtime.cpp` exists. The source ZIP has SHA-256 `892acc2f1f43ec900b0134496eabbbb123e83ba7b71ae4927a546da07c5e52ed`. Run `git -C ports/stardroid/upstream apply --no-index ../xgb35-upstream.patch` from the repository root. The build uses upstream's preparation script for OpenBW. For game assets, use your copy of StarCraft and Brood War. [Upstream and library credits](THIRD_PARTY.md)
