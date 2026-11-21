"""Adapt SuperX V1.1 CFW for XGB35 (480x320 rotated ST7796S) and pack SD deployment tar."""
import json
from pathlib import Path
import re
import tarfile

ROOT = Path(__file__).resolve().parent
SRC = ROOT / 'superx-bundle' / 'extracted'
OUT_TAR = ROOT / 'superx-bundle' / 'superx-xgb35-sd.tar'

THEME_480320 = """
[GENERAL_480320]
font_size = 13
icon_scale = 0.5
batt_x = 390
batt_y = 10
game_count_font_size = 14
game_count_x = 5
game_count_y = 210
game_list_x = 6
game_list_y = 38
game_list_w = 145
items_separation = 14
art_x = 190
art_y = 36
art_max_w = 280
art_max_h = 205
art_text_distance_from_picture = 20
art_text_line_separation = 11
art_text_font_size = 12
system_w = 16
system_h = 16
system_x = -1
system_y = 6
text1_font_size = 14
text1_x = 240
text1_y = 15
text2_font_size = 12
text2_x = 50
text2_y = 285
settings_item_size = 18;
settings_title_size = 28;
settings_item_start_x = 10;
settings_item_start_y = 48;
settings_item_step_y = 24;
"""


def main():
    assert (SRC / 'rootfs.ext3').exists(), 'Missing extracted SuperX-v1.1'

    # 1. Adapt SimplerMenu Plus config.ini for 480x320 & safe 900 MHz ATM7051H clock
    cfg_path = SRC / 'cfw' / 'simplermenu_plus' / 'config.ini'
    cfg = cfg_path.read_text('utf-8', 'replace')
    cfg = re.sub(r'^screenWidth=\d+', 'screenWidth=480', cfg, flags=re.M)
    cfg = re.sub(r'^screenHeight=\d+', 'screenHeight=320', cfg, flags=re.M)
    cfg = re.sub(r'^usbMode=\w+', 'usbMode=ADB', cfg, flags=re.M)
    cfg = re.sub(r'^wifi=\w+', 'wifi=false', cfg, flags=re.M)
    cfg_path.write_text(cfg, encoding='utf-8')

    # 2. Add [GENERAL_480320] section to Epic Noir theme if not already added
    th_path = SRC / 'cfw' / 'simplermenu_plus' / 'themes' / '0 Epic Noir' / 'theme.ini'
    th = th_path.read_text('utf-8', 'replace')
    if '[GENERAL_480320]' not in th:
        th = th.rstrip() + '\n' + THEME_480320
        th_path.write_text(th, encoding='utf-8')

    # 3. Ensure RetroArch config uses video_rotation = "1" (CW matches ST7796S (319-Y, X))
    ra_path = SRC / 'cfw' / 'retroarch' / 'retroarch.cfg'
    ra = ra_path.read_text('utf-8', 'replace')
    ra = re.sub(r'video_rotation\s*=\s*"[0-3]"', 'video_rotation = "1"', ra)
    ra_path.write_text(ra, encoding='utf-8')

    # 4. Pack rootfs.ext3 + apps/ + cfw/ into a clean GNU tar archive (no PAX 'x' headers for BusyBox tar!)
    count = 0
    with tarfile.open(OUT_TAR, 'w', format=tarfile.GNU_FORMAT) as tf:
        tf.add(SRC / 'rootfs.ext3', arcname='rootfs.ext3')
        count += 1
        for folder in ('apps', 'cfw'):
            for p in sorted((SRC / folder).rglob('*')):
                tf.add(p, arcname=p.relative_to(SRC).as_posix(), recursive=False)
                count += 1

    print(json.dumps({'tar': str(OUT_TAR.name), 'entries': count, 'mb': OUT_TAR.stat().st_size // 1048576}, indent=2))


if __name__ == '__main__':
    main()
