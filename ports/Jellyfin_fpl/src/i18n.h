#pragma once
// Localization strings matching streamyfin_my (ru-RU & en).

#include <string>

namespace jf {

struct Strings {
    const char* tab_folders;
    const char* tab_downloads;
    const char* tab_settings;
    const char* offline_badge;

    // Login & Quick Connect
    const char* login_title;
    const char* server_url;
    const char* username;
    const char* password;
    const char* sign_in;
    const char* quick_connect_btn;
    const char* qc_waiting_hint;
    const char* qc_cancel;
    const char* login_error;
    const char* server_unreachable;

    // Folder Browser (from streamyfin_my)
    const char* search_folders;
    const char* clear_search;
    const char* last_played_folder;
    const char* download_folder;
    const char* no_matching_folders;
    const char* no_items;
    const char* watched;
    const char* unwatched;
    const char* stopped_at_prefix;
    const char* retry;
    const char* back;
    const char* loading;
    const char* download_btn;
    const char* downloaded_badge;
    const char* queued_badge;

    // Offline Downloads Browser
    const char* queue_tab_fmt;
    const char* downloaded_tab;
    const char* unknown_folder;
    const char* no_active_downloads;
    const char* no_downloads_yet;
    const char* pause_btn;
    const char* resume_btn;
    const char* cancel_btn;
    const char* delete_btn;
    const char* status_paused;
    const char* status_failed;
    const char* status_queued;
    const char* status_downloading;

    // Settings
    const char* account_section;
    const char* log_out;
    const char* qc_authorize_title;
    const char* qc_authorize_btn;
    const char* qc_authorized_ok;
    const char* qc_authorize_fail;
    const char* playback_section;
    const char* stream_quality;
    const char* download_quality;
    const char* download_original;
    const char* default_audio;
    const char* default_subs;
    const char* audio_output;
    const char* audio_auto;
    const char* audio_hdmi;
    const char* audio_speaker;
    const char* app_language;
    const char* storage_section;
    const char* delete_all_downloads;
    const char* lang_off;
    const char* lang_ru;
    const char* lang_en;
    const char* yes_str;
    const char* no_str;

    // Player (clean discrete controls, zero swipe/touch clutter)
    const char* play;
    const char* pause;
    const char* prev_item;
    const char* next_item;
    const char* audio_track;
    const char* sub_track;
    const char* quality_btn;
    const char* fit_mode;
    const char* fill_mode;
    const char* mark_watched;
    const char* mark_unwatched;
    const char* close_player;
    const char* buffering;
    const char* player_error;

    // On-screen keyboard
    const char* osk_done;
    const char* osk_space;
    const char* osk_bksp;
    const char* osk_clear;
    const char* osk_lang;
};

static constexpr Strings kStringsRu = {
    "Папки",
    "Загрузки",
    "Настройки",
    "Офлайн",

    "Вход в Jellyfin",
    "Адрес сервера (например http://192.168.1.10:8096)",
    "Имя пользователя",
    "Пароль",
    "Войти",
    "Вход по Quick Connect",
    "Введите этот код в приложении Jellyfin -> Quick Connect:",
    "Отмена",
    "Не удалось войти. Проверьте логин/пароль.",
    "Сервер недоступен. Можно перейти во вкладку «Загрузки».",

    "Поиск по имени папки",
    "Очистить",
    "Папка последнего просмотра",
    "Скачать папку целиком",
    "Папки не найдены",
    "Пусто",
    "Просмотрено",
    "Не просмотрено",
    "Остановка на ",
    "Повторить",
    "Назад",
    "Загрузка...",
    "Скачать",
    "Скачано",
    "В очереди",

    "Очередь (%d)",
    "Скачанное",
    "Неизвестная папка",
    "Нет активных загрузок",
    "Нет скачанных файлов",
    "Пауза",
    "Продолжить",
    "Отменить",
    "Удалить",
    "На паузе",
    "Ошибка загрузки",
    "В очереди",
    "Качается",

    "Сервер и аккаунт",
    "Выйти",
    "Quick Connect: авторизовать другое устройство (6 цифр)",
    "Авторизовать код",
    "Устройство авторизовано!",
    "Неверный или истёкший код Quick Connect",
    "Воспроизведение и загрузки",
    "Качество потока",
    "Качество загрузок",
    "Скачивать оригинал без перекодирования",
    "Язык аудио по умолчанию",
    "Субтитры по умолчанию",
    "Аудиовыход приставки",
    "Авто (HDMI -> Динамик)",
    "HDMI",
    "Динамик",
    "Язык интерфейса",
    "Хранилище загрузок",
    "Удалить все загрузки",
    "Выкл.",
    "Русский",
    "English",
    "Да",
    "Нет",

    "Играть",
    "Пауза",
    "Пред.",
    "След.",
    "Звук",
    "Субтитры",
    "Качество",
    "Вписать",
    "Заполнить",
    "Просмотрено",
    "Не просм.",
    "Закрыть",
    "Буферизация...",
    "Ошибка открытия медиапотока",

    "Готово",
    "Пробел",
    "Стереть",
    "Очистить",
    "RU/EN",
};

static constexpr Strings kStringsEn = {
    "Folders",
    "Downloads",
    "Settings",
    "Offline",

    "Sign in to Jellyfin",
    "Server URL (e.g. http://192.168.1.10:8096)",
    "Username",
    "Password",
    "Sign in",
    "Sign in with Quick Connect",
    "Enter this code in your Jellyfin app -> Quick Connect:",
    "Cancel",
    "Authentication failed. Check username or password.",
    "Server unreachable. You can still browse Offline Downloads.",

    "Search folder names",
    "Clear",
    "Folder of last played item",
    "Download entire folder",
    "No matching folders",
    "No items",
    "Watched",
    "Not watched",
    "Stopped at ",
    "Retry",
    "Back",
    "Loading...",
    "Download",
    "Downloaded",
    "Queued",

    "Queue (%d)",
    "Downloaded",
    "Unknown folder",
    "No active downloads",
    "No downloaded files",
    "Pause",
    "Resume",
    "Cancel",
    "Delete",
    "Paused",
    "Download failed",
    "Queued",
    "Downloading",

    "Server & Account",
    "Log out",
    "Quick Connect: authorize another device (6 digits)",
    "Authorize code",
    "Device authorized!",
    "Invalid or expired Quick Connect code",
    "Playback & Downloads",
    "Streaming quality",
    "Download quality",
    "Download original without transcoding",
    "Default audio language",
    "Default subtitles",
    "Console audio output",
    "Auto (HDMI -> Speaker)",
    "HDMI",
    "Speaker",
    "App language",
    "Offline Storage",
    "Delete all downloads",
    "Off",
    "Русский",
    "English",
    "Yes",
    "No",

    "Play",
    "Pause",
    "Prev",
    "Next",
    "Audio",
    "Subtitles",
    "Quality",
    "Fit",
    "Fill",
    "Mark watched",
    "Mark unwatched",
    "Close",
    "Buffering...",
    "Could not open media stream",

    "Done",
    "Space",
    "Bksp",
    "Clear",
    "EN/RU",
};

inline const Strings& tr(const std::string& lang) {
    return (lang == "en") ? kStringsEn : kStringsRu;
}

} // namespace jf
