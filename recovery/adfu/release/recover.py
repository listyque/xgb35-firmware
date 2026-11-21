"""Windows console menu for the XGB35 ADFU recovery kit."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from adfu_read_nand import INTERFACE, PAGE_BYTES, read_nand


TOOLS = Path(__file__).resolve().parent
KIT = TOOLS.parent
PNPUTIL = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/pnputil.exe"
INTERFACE_CLASS = "{FF7B846F-B0A3-45C0-B0FC-D8122086996D}"
BOARD = "LLW7051-057-MAIN-BOARD V2.0"
NAND = "Micron 29F64G08CBABA WP:B"


def find_interface():
    result = subprocess.run(
        [PNPUTIL, "/enum-interfaces", "/enabled", "/class", INTERFACE_CLASS],
        stdout=subprocess.PIPE, check=True,
    )
    paths = [
        value.decode("ascii")
        for value in re.findall(rb"\\\\\?\\USB#[^ \t\r\n]+", result.stdout, re.IGNORECASE)
        if INTERFACE.fullmatch(value.decode("ascii", errors="replace"))
    ]
    if len(paths) != 1:
        raise RuntimeError(f"Нужно одно подключённое устройство ADFU 10D6:10D6. Найдено: {len(paths)}.")
    return paths[0]


def run_tool(name, *args, capture=False, env=None):
    result = subprocess.run(
        [sys.executable, "-B", str(TOOLS / name), *args],
        check=True, stdout=subprocess.PIPE if capture else None, env=env,
    )
    return result.stdout


def check_kit():
    run_tool("adfu_autonomous_service.py", "--dry-run", "--diagnostic")
    run_tool("adfu_write_nand_block0.py", "plan")


def restore():
    print(f"Сверьте маркировку платы: {BOARD}. Память: {NAND}.")
    if input("Если совпали обе маркировки, введите ДА: ").strip().upper() != "ДА":
        return
    check_kit()
    path = find_interface()
    env = dict(os.environ, ACTIONS_ADFU_PNP_PATH=path)
    backups = KIT / "backups"
    backups.mkdir(exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    backup = backups / f"nand-blocks-0-1-{stamp}.bin"
    print("Считываю два загрузочных блока. Это может занять несколько минут.", flush=True)
    read_nand(path, False, fast=False, rows=range(512), out=backup)
    if backup.stat().st_size != 512 * PAGE_BYTES:
        raise RuntimeError("Резервная копия неполная. Запись не началась.")
    print(f"Резервная копия сохранена: {backup}")
    run_tool("adfu_autonomous_service.py", "--diagnostic", env=env)
    report = json.loads(run_tool("adfu_write_nand_block0.py", "inspect", capture=True, env=env))
    if report["bad_block_marker"] != 0xff:
        raise RuntimeError("Метка плохого блока 0. Запись не началась.")
    print(f"Отдельный снимок блока 0: {report['snapshot']}")
    print("Следующая операция сотрёт физический блок 0, запишет образ и проверит чтением.")
    if input("Для запуска записи введите ВОССТАНОВИТЬ: ").strip().upper() != "ВОССТАНОВИТЬ":
        return
    run_tool(
        "adfu_write_nand_block0.py", "rollback",
        "--expect-current", report["current_block0_sha256"],
        "--execute-block0", report["target_hashes"]["rollback"], env=env,
    )
    print("Проверка завершилась. Можно нажать Reset.")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="проверить комплект на ПК без USB")
    if parser.parse_args().check:
        check_kit()
        return
    print("Набор восстановления XGB35. Перед началом откройте START-HERE.ru.html.")
    while True:
        print("\n1. Установить драйвер WinUSB\n2. Проверить подключение ADFU\n"
              "3. Сохранить копию и начать восстановление\n0. Выйти")
        try:
            choice = input("> ").strip()
            if choice == "0":
                return
            if choice == "1":
                subprocess.run(
                    [PNPUTIL, "/add-driver", TOOLS / "adfu-driver/adfuwinusb.inf", "/install"], check=True,
                )
            if choice == "2":
                print("Windows видит ADFU:", find_interface())
            if choice == "3":
                restore()
        except EOFError:
            return
        except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
            print(f"\nОстановлено. Сохраните текст ошибки и файлы резервных копий.\n{error}")


if __name__ == "__main__":
    main()
