#pragma once
// Jellyfin REST API client (libcurl + cJSON) & atomic JSON persistence.

#include "core.h"

#include <curl/curl.h>
#if __has_include(<cjson/cJSON.h>)
#include <cjson/cJSON.h>
#else
#include <cJSON.h>
#endif

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace jf {

inline void ensure_dir(const std::string& p) {
    std::string cur;
    for (size_t i = 0; i < p.size(); ++i) {
        cur.push_back(p[i]);
        if (p[i] == '/' && cur.size() > 1) ::mkdir(cur.c_str(), 0755);
    }
    ::mkdir(p.c_str(), 0755);
}

inline bool write_file_atomic(const std::string& path, const void* data, size_t len) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = (len == 0) || (std::fwrite(data, 1, len, f) == len);
    if (std::fflush(f) != 0) ok = false;
    ::fsync(fileno(f));
    std::fclose(f);
    if (!ok) {
        std::remove(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

inline std::string read_file_text(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    if (sz <= 0 || sz > 32 * 1024 * 1024) { std::fclose(f); return ""; }
    std::fseek(f, 0, SEEK_SET);
    std::string out(size_t(sz), '\0');
    size_t got = std::fread(&out[0], 1, size_t(sz), f);
    std::fclose(f);
    out.resize(got);
    return out;
}

inline std::string jstr(const cJSON* obj, const char* key, const char* def_val = "") {
    const cJSON* it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(it) && it->valuestring) ? std::string(it->valuestring) : std::string(def_val);
}

inline int64_t jint64(const cJSON* obj, const char* key, int64_t def_val = 0) {
    const cJSON* it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(it) ? static_cast<int64_t>(it->valuedouble) : def_val;
}

inline bool jbool(const cJSON* obj, const char* key, bool def_val = false) {
    const cJSON* it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it) != 0;
    return def_val;
}

inline void parse_media_streams(const cJSON* arr, std::vector<MediaStreamInfo>& out) {
    if (!cJSON_IsArray(arr)) return;
    out.clear();
    const cJSON* s = nullptr;
    cJSON_ArrayForEach(s, arr) {
        MediaStreamInfo ms;
        ms.index = int(jint64(s, "Index", -1));
        ms.type = jstr(s, "Type");
        ms.codec = jstr(s, "Codec");
        ms.language = jstr(s, "Language");
        ms.title = jstr(s, "Title");
        ms.display_title = jstr(s, "DisplayTitle");
        ms.is_default = jbool(s, "IsDefault", false);
        ms.is_external = jbool(s, "IsExternal", false);
        ms.is_text_sub = (ms.type == "Subtitle") && (jbool(s, "IsTextSubtitleStream", false) || is_text_subtitle_codec(ms.codec));
        if (ms.index >= 0 && !ms.type.empty()) out.push_back(ms);
    }
}

inline MediaItem parse_base_item(const cJSON* obj) {
    MediaItem m;
    if (!cJSON_IsObject(obj)) return m;
    m.id = jstr(obj, "Id");
    m.name = jstr(obj, "Name");
    m.type = jstr(obj, "Type");
    m.collection_type = jstr(obj, "CollectionType");
    m.media_type = jstr(obj, "MediaType");
    m.parent_id = jstr(obj, "ParentId");
    m.series_id = jstr(obj, "SeriesId");
    m.series_name = jstr(obj, "SeriesName");
    m.season_id = jstr(obj, "SeasonId");
    m.season_name = jstr(obj, "SeasonName");
    m.parent_index_number = int(jint64(obj, "ParentIndexNumber", 0));
    m.index_number = int(jint64(obj, "IndexNumber", 0));
    m.album_id = jstr(obj, "AlbumId");
    m.album_name = jstr(obj, "Album");
    m.container = jstr(obj, "Container");
    m.runtime_ticks = jint64(obj, "RunTimeTicks", 0);
    m.is_folder = jbool(obj, "IsFolder", false) || is_folder_type(m.type);
    m.is_placeholder = jbool(obj, "IsPlaceHolder", false);

    if (const cJSON* ud = cJSON_GetObjectItemCaseSensitive(obj, "UserData")) {
        m.position_ticks = jint64(ud, "PlaybackPositionTicks", 0);
        m.played = jbool(ud, "Played", false);
    }
    if (const cJSON* tags = cJSON_GetObjectItemCaseSensitive(obj, "ImageTags")) {
        m.primary_tag = jstr(tags, "Primary");
        m.thumb_tag = jstr(tags, "Thumb");
    }

    const cJSON* sources = cJSON_GetObjectItemCaseSensitive(obj, "MediaSources");
    if (cJSON_IsArray(sources) && cJSON_GetArraySize(sources) > 0) {
        const cJSON* src0 = cJSON_GetArrayItem(sources, 0);
        m.media_source_id = jstr(src0, "Id", m.id.c_str());
        if (m.container.empty()) m.container = jstr(src0, "Container");
        m.source_bitrate = jint64(src0, "Bitrate", 0);
        if (m.runtime_ticks <= 0) m.runtime_ticks = jint64(src0, "RunTimeTicks", 0);
        parse_media_streams(cJSON_GetObjectItemCaseSensitive(src0, "MediaStreams"), m.streams);
    } else {
        m.media_source_id = m.id;
        parse_media_streams(cJSON_GetObjectItemCaseSensitive(obj, "MediaStreams"), m.streams);
    }
    return m;
}

inline cJSON* serialize_media_item(const MediaItem& m) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "Id", m.id.c_str());
    cJSON_AddStringToObject(o, "Name", m.name.c_str());
    cJSON_AddStringToObject(o, "Type", m.type.c_str());
    cJSON_AddStringToObject(o, "MediaType", m.media_type.c_str());
    cJSON_AddStringToObject(o, "ParentId", m.parent_id.c_str());
    cJSON_AddStringToObject(o, "SeriesId", m.series_id.c_str());
    cJSON_AddStringToObject(o, "SeriesName", m.series_name.c_str());
    cJSON_AddStringToObject(o, "SeasonId", m.season_id.c_str());
    cJSON_AddStringToObject(o, "SeasonName", m.season_name.c_str());
    cJSON_AddNumberToObject(o, "ParentIndexNumber", m.parent_index_number);
    cJSON_AddNumberToObject(o, "IndexNumber", m.index_number);
    cJSON_AddStringToObject(o, "AlbumId", m.album_id.c_str());
    cJSON_AddStringToObject(o, "Album", m.album_name.c_str());
    cJSON_AddStringToObject(o, "Container", m.container.c_str());
    cJSON_AddNumberToObject(o, "RunTimeTicks", double(m.runtime_ticks));

    cJSON* ud = cJSON_CreateObject();
    cJSON_AddNumberToObject(ud, "PlaybackPositionTicks", double(m.position_ticks));
    cJSON_AddBoolToObject(ud, "Played", m.played);
    cJSON_AddItemToObject(o, "UserData", ud);

    cJSON* arr = cJSON_CreateArray();
    for (const auto& s : m.streams) {
        cJSON* so = cJSON_CreateObject();
        cJSON_AddNumberToObject(so, "Index", s.index);
        cJSON_AddStringToObject(so, "Type", s.type.c_str());
        cJSON_AddStringToObject(so, "Codec", s.codec.c_str());
        cJSON_AddStringToObject(so, "Language", s.language.c_str());
        cJSON_AddStringToObject(so, "Title", s.title.c_str());
        cJSON_AddStringToObject(so, "DisplayTitle", s.display_title.c_str());
        cJSON_AddBoolToObject(so, "IsDefault", s.is_default);
        cJSON_AddBoolToObject(so, "IsExternal", s.is_external);
        cJSON_AddBoolToObject(so, "IsTextSubtitleStream", s.is_text_sub);
        cJSON_AddItemToArray(arr, so);
    }
    cJSON_AddItemToObject(o, "MediaStreams", arr);
    return o;
}

// --- Settings & Offline DB Persistence ---

inline void load_app_settings(const std::string& path, AppSettings& st) {
    std::string raw = read_file_text(path);
    if (raw.empty()) return;
    cJSON* root = cJSON_Parse(raw.c_str());
    if (!root) return;
    st.server_url = jstr(root, "server_url", st.server_url.c_str());
    st.user_id = jstr(root, "user_id");
    st.username = jstr(root, "username");
    st.server_name = jstr(root, "server_name");
    st.access_token = jstr(root, "access_token");
    st.device_id = jstr(root, "device_id", st.device_id.c_str());
    st.lang = jstr(root, "lang", "ru");
    st.stream_bitrate = jint64(root, "stream_bitrate", 2000000);
    st.download_bitrate = jint64(root, "download_bitrate", 2000000);
    st.download_original = jbool(root, "download_original", false);
    st.default_audio_lang = jstr(root, "default_audio_lang", "rus");
    st.default_sub_lang = jstr(root, "default_sub_lang", "");
    st.audio_output = int(jint64(root, "audio_output", 0));
    st.video_fit = int(jint64(root, "video_fit", 0));
    st.hw_vol = std::max(0, std::min(40, int(jint64(root, "hw_vol", 32))));
    cJSON_Delete(root);
}

inline bool save_app_settings(const std::string& path, const AppSettings& st) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "server_url", st.server_url.c_str());
    cJSON_AddStringToObject(root, "user_id", st.user_id.c_str());
    cJSON_AddStringToObject(root, "username", st.username.c_str());
    cJSON_AddStringToObject(root, "server_name", st.server_name.c_str());
    cJSON_AddStringToObject(root, "access_token", st.access_token.c_str());
    cJSON_AddStringToObject(root, "device_id", st.device_id.c_str());
    cJSON_AddStringToObject(root, "lang", st.lang.c_str());
    cJSON_AddNumberToObject(root, "stream_bitrate", double(st.stream_bitrate));
    cJSON_AddNumberToObject(root, "download_bitrate", double(st.download_bitrate));
    cJSON_AddBoolToObject(root, "download_original", st.download_original);
    cJSON_AddStringToObject(root, "default_audio_lang", st.default_audio_lang.c_str());
    cJSON_AddStringToObject(root, "default_sub_lang", st.default_sub_lang.c_str());
    cJSON_AddNumberToObject(root, "audio_output", st.audio_output);
    cJSON_AddNumberToObject(root, "video_fit", st.video_fit);
    cJSON_AddNumberToObject(root, "hw_vol", st.hw_vol);
    char* txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return false;
    bool ok = write_file_atomic(path, txt, std::strlen(txt));
    cJSON_free(txt);
    return ok;
}

inline void load_downloads_db(
    const std::string& path,
    std::vector<DownloadedEntry>& downloaded,
    std::vector<QueueEntry>& queue
) {
    downloaded.clear();
    queue.clear();
    std::string raw = read_file_text(path);
    if (raw.empty()) return;
    cJSON* root = cJSON_Parse(raw.c_str());
    if (!root) return;

    const cJSON* darr = cJSON_GetObjectItemCaseSensitive(root, "downloaded");
    if (cJSON_IsArray(darr)) {
        const cJSON* el = nullptr;
        cJSON_ArrayForEach(el, darr) {
            DownloadedEntry d;
            d.item = parse_base_item(cJSON_GetObjectItemCaseSensitive(el, "item"));
            d.local_media_path = jstr(el, "local_media_path");
            d.local_thumb_path = jstr(el, "local_thumb_path");
            d.file_bytes = jint64(el, "file_bytes", 0);
            d.has_folder_path = jbool(el, "has_folder_path", false);
            const cJSON* fp = cJSON_GetObjectItemCaseSensitive(el, "folder_path");
            if (cJSON_IsArray(fp)) {
                const cJSON* f = nullptr;
                cJSON_ArrayForEach(f, fp) {
                    d.folder_path.push_back({jstr(f, "id"), jstr(f, "name")});
                }
            }
            const cJSON* subs = cJSON_GetObjectItemCaseSensitive(el, "local_subs");
            if (cJSON_IsObject(subs)) {
                const cJSON* sp = nullptr;
                cJSON_ArrayForEach(sp, subs) {
                    if (sp->string && cJSON_IsString(sp) && sp->valuestring)
                        d.local_subs[std::atoi(sp->string)] = sp->valuestring;
                }
            }
            struct stat st{};
            if (!d.item.id.empty() && !d.local_media_path.empty() && ::stat(d.local_media_path.c_str(), &st) == 0)
                downloaded.push_back(std::move(d));
        }
    }

    const cJSON* qarr = cJSON_GetObjectItemCaseSensitive(root, "queue");
    if (cJSON_IsArray(qarr)) {
        const cJSON* el = nullptr;
        cJSON_ArrayForEach(el, qarr) {
            QueueEntry q;
            q.item = parse_base_item(cJSON_GetObjectItemCaseSensitive(el, "item"));
            q.max_bitrate = jint64(el, "max_bitrate", 2000000);
            q.original = jbool(el, "original", false);
            int st_val = int(jint64(el, "status", 0));
            // An interrupted in-flight download resumes cleanly as Queued on restart
            q.status = (st_val == int(QueueStatus::Paused)) ? QueueStatus::Paused
                     : (st_val == int(QueueStatus::Failed)) ? QueueStatus::Failed
                     : QueueStatus::Queued;
            q.error_msg = jstr(el, "error_msg");
            q.has_folder_path = jbool(el, "has_folder_path", false);
            const cJSON* fp = cJSON_GetObjectItemCaseSensitive(el, "folder_path");
            if (cJSON_IsArray(fp)) {
                const cJSON* f = nullptr;
                cJSON_ArrayForEach(f, fp) {
                    q.folder_path.push_back({jstr(f, "id"), jstr(f, "name")});
                }
            }
            if (!q.item.id.empty()) queue.push_back(std::move(q));
        }
    }
    cJSON_Delete(root);
}

inline bool save_downloads_db(
    const std::string& path,
    const std::vector<DownloadedEntry>& downloaded,
    const std::vector<QueueEntry>& queue
) {
    cJSON* root = cJSON_CreateObject();
    cJSON* darr = cJSON_CreateArray();
    for (const auto& d : downloaded) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddItemToObject(o, "item", serialize_media_item(d.item));
        cJSON_AddStringToObject(o, "local_media_path", d.local_media_path.c_str());
        cJSON_AddStringToObject(o, "local_thumb_path", d.local_thumb_path.c_str());
        cJSON_AddNumberToObject(o, "file_bytes", double(d.file_bytes));
        cJSON_AddBoolToObject(o, "has_folder_path", d.has_folder_path);
        cJSON* fp = cJSON_CreateArray();
        for (const auto& f : d.folder_path) {
            cJSON* fo = cJSON_CreateObject();
            cJSON_AddStringToObject(fo, "id", f.id.c_str());
            cJSON_AddStringToObject(fo, "name", f.name.c_str());
            cJSON_AddItemToArray(fp, fo);
        }
        cJSON_AddItemToObject(o, "folder_path", fp);
        cJSON* subs = cJSON_CreateObject();
        for (const auto& kv : d.local_subs)
            cJSON_AddStringToObject(subs, std::to_string(kv.first).c_str(), kv.second.c_str());
        cJSON_AddItemToObject(o, "local_subs", subs);
        cJSON_AddItemToArray(darr, o);
    }
    cJSON_AddItemToObject(root, "downloaded", darr);

    cJSON* qarr = cJSON_CreateArray();
    for (const auto& q : queue) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddItemToObject(o, "item", serialize_media_item(q.item));
        cJSON_AddNumberToObject(o, "max_bitrate", double(q.max_bitrate));
        cJSON_AddBoolToObject(o, "original", q.original);
        cJSON_AddNumberToObject(o, "status", int(q.status));
        cJSON_AddStringToObject(o, "error_msg", q.error_msg.c_str());
        cJSON_AddBoolToObject(o, "has_folder_path", q.has_folder_path);
        cJSON* fp = cJSON_CreateArray();
        for (const auto& f : q.folder_path) {
            cJSON* fo = cJSON_CreateObject();
            cJSON_AddStringToObject(fo, "id", f.id.c_str());
            cJSON_AddStringToObject(fo, "name", f.name.c_str());
            cJSON_AddItemToArray(fp, fo);
        }
        cJSON_AddItemToObject(o, "folder_path", fp);
        cJSON_AddItemToArray(qarr, o);
    }
    cJSON_AddItemToObject(root, "queue", qarr);

    char* txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return false;
    bool ok = write_file_atomic(path, txt, std::strlen(txt));
    cJSON_free(txt);
    return ok;
}

// --- Negotiated Stream Info ---

struct NegotiatedStream {
    std::string url;
    std::string play_session_id;
    std::string media_source_id;
    std::string container;
    bool is_transcoding = false;
    std::vector<MediaStreamInfo> streams;
};

// --- Jellyfin HTTP/REST Client ---

class JellyfinClient {
public:
    std::string ca_bundle_path;
    mutable std::mutex auth_mu;
    std::string base_url;
    std::string user_id;
    std::string token;
    std::string device_id = "xgb35-fpl";

    void configure(const AppSettings& st) {
        std::lock_guard<std::mutex> lk(auth_mu);
        base_url = normalize_server_url(st.server_url);
        user_id = st.user_id;
        token = st.access_token;
        if (!st.device_id.empty()) device_id = st.device_id;
    }

    bool has_session() const {
        std::lock_guard<std::mutex> lk(auth_mu);
        return !base_url.empty() && !user_id.empty() && !token.empty();
    }

    struct HttpResp {
        long status = 0;
        std::string body;
        std::string err;
    };

    HttpResp request(
        const char* method,
        const std::string& full_or_rel_url,
        const std::string& json_body = "",
        long timeout_sec = 20,
        const std::string& override_base = ""
    ) const {
        HttpResp res;
        std::string url, auth_hdr;
        {
            std::lock_guard<std::mutex> lk(auth_mu);
            std::string b = override_base.empty() ? base_url : normalize_server_url(override_base);
            if (full_or_rel_url.rfind("http://", 0) == 0 || full_or_rel_url.rfind("https://", 0) == 0)
                url = full_or_rel_url;
            else
                url = b + full_or_rel_url;
            auth_hdr = "X-Emby-Authorization: " + build_emby_auth_header(device_id, override_base.empty() ? token : "");
        }

        CURL* curl = curl_easy_init();
        if (!curl) { res.err = "curl_easy_init failed"; return res; }

        struct curl_slist* hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, auth_hdr.c_str());
        hdrs = curl_slist_append(hdrs, "Accept: application/json");
        if (!json_body.empty() || std::strcmp(method, "POST") == 0)
            hdrs = curl_slist_append(hdrs, "Content-Type: application/json");

        auto write_cb = +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            size_t total = size * nmemb;
            auto* out = static_cast<std::string*>(userdata);
            if (out->size() + total > 32 * 1024 * 1024) return 0;
            out->append(ptr, total);
            return total;
        };

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        if (!ca_bundle_path.empty() && ::access(ca_bundle_path.c_str(), R_OK) == 0)
            curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle_path.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);

        if (std::strcmp(method, "POST") == 0) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(json_body.size()));
        } else if (std::strcmp(method, "DELETE") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }

        CURLcode cc = curl_easy_perform(curl);
        if (cc == CURLE_OK)
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
        else
            res.err = curl_easy_strerror(cc);

        curl_slist_free_all(hdrs);
        curl_easy_cleanup(curl);
        return res;
    }

    bool ping_server(const std::string& server, std::string* server_name_out) const {
        auto r = request("GET", "/System/Info/Public", "", 8, server);
        if (r.status < 200 || r.status >= 300) return false;
        cJSON* j = cJSON_Parse(r.body.c_str());
        if (!j) return false;
        if (server_name_out) *server_name_out = jstr(j, "ServerName", "Jellyfin");
        cJSON_Delete(j);
        return true;
    }

    bool login_password(
        const std::string& server,
        const std::string& user,
        const std::string& pass,
        AppSettings& st_out
    ) {
        std::string norm = normalize_server_url(server);
        if (norm.empty() || user.empty()) return false;
        std::string srv_name = "Jellyfin";
        ping_server(norm, &srv_name);

        cJSON* req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "Username", user.c_str());
        cJSON_AddStringToObject(req, "Pw", pass.c_str());
        char* body = cJSON_PrintUnformatted(req);
        cJSON_Delete(req);
        auto r = request("POST", "/Users/AuthenticateByName", body ? body : "{}", 15, norm);
        if (body) cJSON_free(body);
        if (r.status < 200 || r.status >= 300) return false;

        cJSON* resp = cJSON_Parse(r.body.c_str());
        if (!resp) return false;
        std::string tok = jstr(resp, "AccessToken");
        const cJSON* uobj = cJSON_GetObjectItemCaseSensitive(resp, "User");
        std::string uid = uobj ? jstr(uobj, "Id") : "";
        std::string uname = uobj ? jstr(uobj, "Name", user.c_str()) : user;
        cJSON_Delete(resp);
        if (tok.empty() || uid.empty()) return false;

        st_out.server_url = norm;
        st_out.server_name = srv_name;
        st_out.access_token = tok;
        st_out.user_id = uid;
        st_out.username = uname;
        configure(st_out);
        return true;
    }

    bool qc_initiate(const std::string& server, std::string& secret_out, std::string& code_out) const {
        std::string norm = normalize_server_url(server);
        if (norm.empty()) return false;
        auto r = request("POST", "/QuickConnect/Initiate", "", 12, norm);
        if (r.status < 200 || r.status >= 300) return false;
        cJSON* j = cJSON_Parse(r.body.c_str());
        if (!j) return false;
        secret_out = jstr(j, "Secret");
        code_out = jstr(j, "Code");
        cJSON_Delete(j);
        return !secret_out.empty() && !code_out.empty();
    }

    // Returns: 0 = still waiting, 1 = authenticated & logged in, -1 = error/expired
    int qc_poll_and_finish(const std::string& server, const std::string& secret, AppSettings& st_out) {
        std::string norm = normalize_server_url(server);
        auto r = request("GET", "/QuickConnect/Connect?secret=" + url_encode(secret), "", 10, norm);
        if (r.status < 200 || r.status >= 300) return -1;
        cJSON* j = cJSON_Parse(r.body.c_str());
        if (!j) return -1;
        bool authed = jbool(j, "Authenticated", false);
        cJSON_Delete(j);
        if (!authed) return 0;

        cJSON* req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "Secret", secret.c_str());
        char* body = cJSON_PrintUnformatted(req);
        cJSON_Delete(req);
        auto ar = request("POST", "/Users/AuthenticateWithQuickConnect", body ? body : "{}", 15, norm);
        if (body) cJSON_free(body);
        if (ar.status < 200 || ar.status >= 300) return -1;

        cJSON* resp = cJSON_Parse(ar.body.c_str());
        if (!resp) return -1;
        std::string tok = jstr(resp, "AccessToken");
        const cJSON* uobj = cJSON_GetObjectItemCaseSensitive(resp, "User");
        std::string uid = uobj ? jstr(uobj, "Id") : "";
        std::string uname = uobj ? jstr(uobj, "Name") : "";
        cJSON_Delete(resp);
        if (tok.empty() || uid.empty()) return -1;

        std::string srv_name = "Jellyfin";
        ping_server(norm, &srv_name);
        st_out.server_url = norm;
        st_out.server_name = srv_name;
        st_out.access_token = tok;
        st_out.user_id = uid;
        st_out.username = uname;
        configure(st_out);
        return 1;
    }

    bool qc_authorize(const std::string& code) const {
        if (code.empty()) return false;
        auto r = request("POST", "/QuickConnect/Authorize?code=" + url_encode(code), "", 12);
        return r.status >= 200 && r.status < 300;
    }

    bool get_views(std::vector<MediaItem>& views_out) const {
        std::string uid;
        { std::lock_guard<std::mutex> lk(auth_mu); uid = user_id; }
        auto r = request("GET", "/Users/" + url_encode(uid) + "/Views");
        if (r.status < 200 || r.status >= 300) return false;
        cJSON* j = cJSON_Parse(r.body.c_str());
        if (!j) return false;
        views_out.clear();
        const cJSON* arr = cJSON_GetObjectItemCaseSensitive(j, "Items");
        if (cJSON_IsArray(arr)) {
            const cJSON* el = nullptr;
            cJSON_ArrayForEach(el, arr) {
                MediaItem m = parse_base_item(el);
                if (opens_as_folders(m)) views_out.push_back(std::move(m));
            }
        }
        cJSON_Delete(j);
        return true;
    }

    bool get_last_played_folder(MediaItem& folder_out) const {
        std::string uid;
        { std::lock_guard<std::mutex> lk(auth_mu); uid = user_id; }
        std::string q = "/Users/" + url_encode(uid) +
                        "/Items?IncludeItemTypes=Movie,Episode,Video,MusicVideo,Audio,Trailer"
                        "&Recursive=true&SortBy=DatePlayed&SortOrder=Descending"
                        "&Fields=ParentId&EnableUserData=true&EnableTotalRecordCount=false&Limit=1";
        auto r = request("GET", q);
        if (r.status < 200 || r.status >= 300) return false;
        cJSON* j = cJSON_Parse(r.body.c_str());
        if (!j) return false;
        std::string parent_id;
        const cJSON* arr = cJSON_GetObjectItemCaseSensitive(j, "Items");
        if (cJSON_IsArray(arr) && cJSON_GetArraySize(arr) > 0) {
            const cJSON* it = cJSON_GetArrayItem(arr, 0);
            const cJSON* ud = cJSON_GetObjectItemCaseSensitive(it, "UserData");
            if (ud && !jstr(ud, "LastPlayedDate").empty()) {
                parent_id = jstr(it, "ParentId");
                if (parent_id.empty()) parent_id = jstr(it, "SeasonId");
                if (parent_id.empty()) parent_id = jstr(it, "AlbumId");
            }
        }
        cJSON_Delete(j);
        if (parent_id.empty()) return false;

        auto pr = request("GET", "/Users/" + url_encode(uid) + "/Items/" + url_encode(parent_id));
        if (pr.status < 200 || pr.status >= 300) return false;
        cJSON* pj = cJSON_Parse(pr.body.c_str());
        if (!pj) return false;
        folder_out = parse_base_item(pj);
        cJSON_Delete(pj);
        return !folder_out.id.empty();
    }

    bool get_folder_page(
        const std::string& parent_id,
        int start_index,
        int limit,
        std::vector<MediaItem>& items_out
    ) const {
        std::string uid;
        { std::lock_guard<std::mutex> lk(auth_mu); uid = user_id; }
        std::string q = "/Users/" + url_encode(uid) + "/Items?Recursive=false&CollapseBoxSetItems=false"
                        "&Fields=MediaSources,MediaStreams,ParentId&EnableUserData=true"
                        "&SortBy=IsFolder,SortName&EnableTotalRecordCount=false"
                        "&StartIndex=" + std::to_string(start_index) +
                        "&Limit=" + std::to_string(limit);
        if (!parent_id.empty()) q += "&ParentId=" + url_encode(parent_id);
        auto r = request("GET", q);
        if (r.status < 200 || r.status >= 300) return false;
        cJSON* j = cJSON_Parse(r.body.c_str());
        if (!j) return false;
        items_out.clear();
        const cJSON* arr = cJSON_GetObjectItemCaseSensitive(j, "Items");
        if (cJSON_IsArray(arr)) {
            const cJSON* el = nullptr;
            cJSON_ArrayForEach(el, arr) {
                MediaItem m = parse_base_item(el);
                if (!m.id.empty()) items_out.push_back(std::move(m));
            }
        }
        cJSON_Delete(j);
        return true;
    }

    bool load_recursive_items(
        const std::string& parent_id,
        bool folders_only,
        std::vector<MediaItem>& out
    ) const {
        std::string uid;
        { std::lock_guard<std::mutex> lk(auth_mu); uid = user_id; }
        out.clear();
        std::map<std::string, size_t> seen;
        const int page_sz = 200;
        const char* types = folders_only
            ? "Folder,CollectionFolder,PhotoAlbum,MusicAlbum,Series,Season"
            : "Movie,Episode,Video,MusicVideo,Audio,Trailer";
        for (int start = 0;; start += page_sz) {
            std::string q = "/Users/" + url_encode(uid) + "/Items?Recursive=true&CollapseBoxSetItems=false"
                            "&IncludeItemTypes=" + std::string(types) +
                            "&SortBy=SortName&EnableTotalRecordCount=false"
                            "&StartIndex=" + std::to_string(start) +
                            "&Limit=" + std::to_string(page_sz);
            if (!folders_only)
                q += "&Fields=MediaSources,MediaStreams,ParentId&EnableUserData=true&IsPlaceHolder=false";
            else
                q += "&EnableUserData=false";
            if (!parent_id.empty()) q += "&ParentId=" + url_encode(parent_id);

            auto r = request("GET", q, "", 30);
            if (r.status < 200 || r.status >= 300) return !out.empty();
            cJSON* j = cJSON_Parse(r.body.c_str());
            if (!j) return !out.empty();
            const cJSON* arr = cJSON_GetObjectItemCaseSensitive(j, "Items");
            int got_raw = 0;
            size_t prev_cnt = out.size();
            if (cJSON_IsArray(arr)) {
                const cJSON* el = nullptr;
                cJSON_ArrayForEach(el, arr) {
                    ++got_raw;
                    MediaItem m = parse_base_item(el);
                    if (m.id.empty() || m.is_placeholder) continue;
                    if (folders_only && !is_folder_type(m.type)) continue;
                    if (!folders_only && !is_downloadable_type(m.type)) continue;
                    if (seen.find(m.id) == seen.end()) {
                        seen[m.id] = out.size();
                        out.push_back(std::move(m));
                    }
                }
            }
            cJSON_Delete(j);
            if (got_raw < page_sz || out.size() == prev_cnt) break;
        }
        return true;
    }

    bool get_ancestors_folder_path(const std::string& item_id, std::vector<DownloadFolder>& out) const {
        std::string uid;
        { std::lock_guard<std::mutex> lk(auth_mu); uid = user_id; }
        auto r = request("GET", "/Items/" + url_encode(item_id) + "/Ancestors?userId=" + url_encode(uid), "", 12);
        if (r.status < 200 || r.status >= 300) return false;
        cJSON* arr = cJSON_Parse(r.body.c_str());
        if (!cJSON_IsArray(arr)) { if (arr) cJSON_Delete(arr); return false; }
        out.clear();
        int n = cJSON_GetArraySize(arr);
        for (int i = n - 1; i >= 0; --i) {
            const cJSON* p = cJSON_GetArrayItem(arr, i);
            std::string pid = jstr(p, "Id");
            std::string pname = jstr(p, "Name");
            std::string ptype = jstr(p, "Type");
            if (!pid.empty() && !pname.empty() &&
                (is_folder_type(ptype) || ptype == "UserView" || ptype == "MusicArtist")) {
                out.push_back({pid, pname});
            }
        }
        cJSON_Delete(arr);
        return true;
    }

    bool negotiate_stream(
        const MediaItem& item,
        int64_t start_ticks,
        int64_t max_bitrate,
        int audio_idx,
        int sub_idx,
        bool for_download,
        NegotiatedStream& out
    ) const {
        std::string b_url, uid, tok, dev;
        {
            std::lock_guard<std::mutex> lk(auth_mu);
            b_url = base_url; uid = user_id; tok = token; dev = device_id;
        }
        if (b_url.empty() || uid.empty() || item.id.empty()) return false;

        if (for_download && max_bitrate <= 0) {
            out.url = b_url + "/Items/" + url_encode(item.id) + "/Download?api_key=" + url_encode(tok);
            out.media_source_id = item.media_source_id.empty() ? item.id : item.media_source_id;
            out.container = item.container.empty() ? "mp4" : item.container;
            out.is_transcoding = false;
            out.streams = item.streams;
            return true;
        }

        const auto& preset = get_bitrate_preset(max_bitrate);
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "UserId", uid.c_str());
        cJSON_AddNumberToObject(root, "StartTimeTicks", double(start_ticks));
        cJSON_AddBoolToObject(root, "IsPlayback", !for_download);
        cJSON_AddBoolToObject(root, "AutoOpenLiveStream", true);
        if (max_bitrate > 0) cJSON_AddNumberToObject(root, "MaxStreamingBitrate", double(max_bitrate));
        if (audio_idx >= 0) cJSON_AddNumberToObject(root, "AudioStreamIndex", audio_idx);
        if (sub_idx >= -1) cJSON_AddNumberToObject(root, "SubtitleStreamIndex", sub_idx);
        if (!item.media_source_id.empty()) cJSON_AddStringToObject(root, "MediaSourceId", item.media_source_id.c_str());

        cJSON* prof = cJSON_CreateObject();
        cJSON_AddStringToObject(prof, "Name", for_download ? "Jellyfin_fpl Download" : "Jellyfin_fpl XGB35");
        if (max_bitrate > 0) {
            cJSON_AddNumberToObject(prof, "MaxStreamingBitrate", double(max_bitrate));
            cJSON_AddNumberToObject(prof, "MaxStaticBitrate", double(max_bitrate));
        }

        // DirectPlayProfiles: allow direct play when Max (0) or source fits within requested cap & 8-bit H.264
        cJSON* dp_arr = cJSON_CreateArray();
        {
            cJSON* dp_aud = cJSON_CreateObject();
            cJSON_AddStringToObject(dp_aud, "Type", "Audio");
            cJSON_AddStringToObject(dp_aud, "Container", "mp3,flac,ogg,opus,wav,m4a,aac,webma");
            cJSON_AddItemToArray(dp_arr, dp_aud);

            cJSON* dp_vid = cJSON_CreateObject();
            cJSON_AddStringToObject(dp_vid, "Type", "Video");
            if (max_bitrate <= 0) {
                cJSON_AddStringToObject(dp_vid, "Container", "mp4,m4v,mkv,webm,mov,avi,ts");
            } else {
                cJSON_AddStringToObject(dp_vid, "Container", "mp4,m4v,mkv,ts");
                cJSON_AddStringToObject(dp_vid, "VideoCodec", "h264,mpeg4");
                cJSON_AddStringToObject(dp_vid, "AudioCodec", "aac,mp3,ac3,opus,vorbis");
            }
            cJSON_AddItemToArray(dp_arr, dp_vid);
        }
        cJSON_AddItemToObject(prof, "DirectPlayProfiles", dp_arr);

        // TranscodingProfiles: progressive HTTP MPEG-TS for instant low-RAM streaming, MP4 for downloads
        cJSON* tp_arr = cJSON_CreateArray();
        {
            cJSON* tp_aud = cJSON_CreateObject();
            cJSON_AddStringToObject(tp_aud, "Type", "Audio");
            cJSON_AddStringToObject(tp_aud, "Container", "mp3");
            cJSON_AddStringToObject(tp_aud, "AudioCodec", "mp3");
            cJSON_AddStringToObject(tp_aud, "Protocol", "http");
            cJSON_AddStringToObject(tp_aud, "Context", for_download ? "Static" : "Streaming");
            cJSON_AddStringToObject(tp_aud, "MaxAudioChannels", "2");
            cJSON_AddItemToArray(tp_arr, tp_aud);

            cJSON* tp_vid = cJSON_CreateObject();
            cJSON_AddStringToObject(tp_vid, "Type", "Video");
            cJSON_AddStringToObject(tp_vid, "Container", for_download ? "mp4" : "ts");
            cJSON_AddStringToObject(tp_vid, "VideoCodec", "h264");
            cJSON_AddStringToObject(tp_vid, "AudioCodec", "aac,mp3");
            cJSON_AddStringToObject(tp_vid, "Protocol", "http");
            cJSON_AddStringToObject(tp_vid, "Context", for_download ? "Static" : "Streaming");
            cJSON_AddStringToObject(tp_vid, "MaxAudioChannels", "2");
            cJSON_AddItemToArray(tp_arr, tp_vid);
        }
        cJSON_AddItemToObject(prof, "TranscodingProfiles", tp_arr);

        // CodecProfiles: cap resolution & 8-bit depth when a transcode preset is selected
        if (max_bitrate > 0 && preset.max_w > 0) {
            cJSON* cp_arr = cJSON_CreateArray();
            cJSON* cp = cJSON_CreateObject();
            cJSON_AddStringToObject(cp, "Type", "Video");
            cJSON_AddStringToObject(cp, "Codec", "h264");
            cJSON* conds = cJSON_CreateArray();
            auto add_cond = [&](const char* prop, const char* val) {
                cJSON* c = cJSON_CreateObject();
                cJSON_AddStringToObject(c, "Condition", "LessThanEqual");
                cJSON_AddStringToObject(c, "Property", prop);
                cJSON_AddStringToObject(c, "Value", val);
                cJSON_AddBoolToObject(c, "IsRequired", false);
                cJSON_AddItemToArray(conds, c);
            };
            add_cond("Width", std::to_string(preset.max_w).c_str());
            add_cond("Height", std::to_string(preset.max_h).c_str());
            add_cond("VideoBitDepth", "8");
            cJSON_AddItemToObject(cp, "Conditions", conds);
            cJSON_AddItemToArray(cp_arr, cp);
            cJSON_AddItemToObject(prof, "CodecProfiles", cp_arr);
        }

        // SubtitleProfiles: text subs delivered externally as VTT/SRT, bitmap subs burned in when transcoding
        cJSON* sp_arr = cJSON_CreateArray();
        for (const char* fmt : {"vtt", "srt", "subrip", "ass", "ssa"}) {
            cJSON* sp = cJSON_CreateObject();
            cJSON_AddStringToObject(sp, "Format", fmt);
            cJSON_AddStringToObject(sp, "Method", "External");
            cJSON_AddItemToArray(sp_arr, sp);
        }
        for (const char* fmt : {"pgssub", "dvdsub", "dvbsub"}) {
            cJSON* sp = cJSON_CreateObject();
            cJSON_AddStringToObject(sp, "Format", fmt);
            cJSON_AddStringToObject(sp, "Method", "Encode");
            cJSON_AddItemToArray(sp_arr, sp);
        }
        cJSON_AddItemToObject(prof, "SubtitleProfiles", sp_arr);
        cJSON_AddItemToObject(root, "DeviceProfile", prof);

        char* req_txt = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        auto r = request("POST", "/Items/" + url_encode(item.id) + "/PlaybackInfo?UserId=" + url_encode(uid), req_txt ? req_txt : "{}", 20);
        if (req_txt) cJSON_free(req_txt);
        if (r.status < 200 || r.status >= 300) return false;

        cJSON* resp = cJSON_Parse(r.body.c_str());
        if (!resp) return false;
        out.play_session_id = jstr(resp, "PlaySessionId");
        const cJSON* sources = cJSON_GetObjectItemCaseSensitive(resp, "MediaSources");
        if (!cJSON_IsArray(sources) || cJSON_GetArraySize(sources) == 0) {
            cJSON_Delete(resp);
            return false;
        }
        const cJSON* src0 = cJSON_GetArrayItem(sources, 0);
        out.media_source_id = jstr(src0, "Id", item.id.c_str());
        out.container = jstr(src0, "Container", "mp4");
        parse_media_streams(cJSON_GetObjectItemCaseSensitive(src0, "MediaStreams"), out.streams);
        std::string trans_url = jstr(src0, "TranscodingUrl");
        cJSON_Delete(resp);

        if (!trans_url.empty()) {
            out.is_transcoding = true;
            out.container = for_download ? "mp4" : "ts";
            out.url = (trans_url.rfind("http", 0) == 0) ? trans_url : (b_url + trans_url);
        } else {
            out.is_transcoding = false;
            const char* kind_path = is_audio_only(item) ? "/Audio/" : "/Videos/";
            out.url = b_url + kind_path + url_encode(item.id) + "/stream?static=true"
                      "&mediaSourceId=" + url_encode(out.media_source_id) +
                      "&deviceId=" + url_encode(dev) +
                      "&api_key=" + url_encode(tok);
            if (!out.play_session_id.empty())
                out.url += "&playSessionId=" + url_encode(out.play_session_id);
        }
        return true;
    }

    void stop_active_encoding(const std::string& play_session_id) const {
        if (play_session_id.empty()) return;
        std::string dev;
        { std::lock_guard<std::mutex> lk(auth_mu); dev = device_id; }
        request("DELETE", "/Videos/ActiveEncodings?deviceId=" + url_encode(dev) +
                          "&playSessionId=" + url_encode(play_session_id), "", 6);
    }

    std::string fetch_subtitle_text(const std::string& item_id, const std::string& media_source_id, int sub_idx) const {
        if (sub_idx < 0 || item_id.empty()) return "";
        std::string tok;
        { std::lock_guard<std::mutex> lk(auth_mu); tok = token; }
        std::string msid = media_source_id.empty() ? item_id : media_source_id;
        for (const char* ext : {"Stream.vtt", "Stream.srt"}) {
            std::string path = "/Videos/" + url_encode(item_id) + "/" + url_encode(msid) +
                               "/Subtitles/" + std::to_string(sub_idx) + "/" + ext + "?api_key=" + url_encode(tok);
            auto r = request("GET", path, "", 15);
            if (r.status >= 200 && r.status < 300 && !r.body.empty()) return r.body;
        }
        return "";
    }

    void report_playback(
        const char* stage, // "Start", "Progress", "Stopped"
        const std::string& item_id,
        const std::string& media_source_id,
        const std::string& play_session_id,
        int64_t pos_ticks,
        bool is_paused
    ) const {
        if (item_id.empty()) return;
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ItemId", item_id.c_str());
        if (!media_source_id.empty()) cJSON_AddStringToObject(o, "MediaSourceId", media_source_id.c_str());
        if (!play_session_id.empty()) cJSON_AddStringToObject(o, "PlaySessionId", play_session_id.c_str());
        cJSON_AddNumberToObject(o, "PositionTicks", double(std::max<int64_t>(0, pos_ticks)));
        cJSON_AddBoolToObject(o, "IsPaused", is_paused);
        cJSON_AddBoolToObject(o, "CanSeek", true);
        char* body = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        const char* ep = (std::strcmp(stage, "Start") == 0) ? "/Sessions/Playing"
                       : (std::strcmp(stage, "Stopped") == 0) ? "/Sessions/Playing/Stopped"
                       : "/Sessions/Playing/Progress";
        request("POST", ep, body ? body : "{}", 8);
        if (body) cJSON_free(body);
    }

    bool set_played(const std::string& item_id, bool played) const {
        std::string uid;
        { std::lock_guard<std::mutex> lk(auth_mu); uid = user_id; }
        if (uid.empty() || item_id.empty()) return false;
        auto r = request(played ? "POST" : "DELETE",
                         "/Users/" + url_encode(uid) + "/PlayedItems/" + url_encode(item_id), "", 10);
        return r.status >= 200 && r.status < 300;
    }

    bool fetch_thumbnail_bytes(const MediaItem& item, std::string& bytes_out) const {
        if (item.id.empty()) return false;
        bool use_thumb = !item.thumb_tag.empty();
        if (!use_thumb && item.primary_tag.empty()) return false;
        std::string path = "/Items/" + url_encode(item.id) + "/Images/" +
                           (use_thumb ? "Thumb" : "Primary") +
                           "?fillWidth=192&fillHeight=108&quality=78";
        auto r = request("GET", path, "", 10);
        if (r.status < 200 || r.status >= 300 || r.body.size() < 64) return false;
        bytes_out = std::move(r.body);
        return true;
    }

    bool download_to_file(
        const std::string& url,
        const std::string& dest_path,
        std::atomic<bool>& abort_flag,
        const std::function<void(int64_t dl_now, int64_t dl_total)>& on_progress,
        int64_t* final_bytes_out,
        std::string* err_out
    ) const {
        std::string auth_hdr;
        {
            std::lock_guard<std::mutex> lk(auth_mu);
            auth_hdr = "X-Emby-Authorization: " + build_emby_auth_header(device_id, token);
        }
        std::string part_path = dest_path + ".part";
        FILE* f = std::fopen(part_path.c_str(), "wb");
        if (!f) {
            if (err_out) *err_out = "Cannot open destination file";
            return false;
        }

        struct DlCtx {
            FILE* fp;
            std::atomic<bool>* abort;
            const std::function<void(int64_t, int64_t)>* cb;
            int64_t written = 0;
            bool write_err = false;
        } ctx{f, &abort_flag, &on_progress, 0, false};

        CURL* curl = curl_easy_init();
        if (!curl) {
            std::fclose(f);
            std::remove(part_path.c_str());
            if (err_out) *err_out = "curl init failed";
            return false;
        }

        struct curl_slist* hdrs = curl_slist_append(nullptr, auth_hdr.c_str());
        auto write_cb = +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            auto* c = static_cast<DlCtx*>(userdata);
            if (c->abort->load(std::memory_order_relaxed)) return 0;
            size_t total = size * nmemb;
            size_t w = std::fwrite(ptr, 1, total, c->fp);
            if (w != total) { c->write_err = true; return 0; }
            c->written += int64_t(w);
            return w;
        };
        auto xfer_cb = +[](void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) -> int {
            auto* c = static_cast<DlCtx*>(clientp);
            if (c->abort->load(std::memory_order_relaxed)) return 1;
            if (c->cb && *c->cb) (*c->cb)(int64_t(dlnow > 0 ? dlnow : c->written), int64_t(dltotal));
            return 0;
        };

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 45L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        if (!ca_bundle_path.empty() && ::access(ca_bundle_path.c_str(), R_OK) == 0)
            curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle_path.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);

        CURLcode cc = curl_easy_perform(curl);
        long status = 0;
        if (cc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(curl);

        std::fflush(f);
        ::fsync(fileno(f));
        std::fclose(f);

        if (abort_flag.load(std::memory_order_relaxed) || cc != CURLE_OK || ctx.write_err || status < 200 || status >= 300 || ctx.written <= 0) {
            std::remove(part_path.c_str());
            if (err_out) {
                if (abort_flag.load()) *err_out = "aborted";
                else if (ctx.write_err) *err_out = "disk full / write error";
                else if (cc != CURLE_OK) *err_out = curl_easy_strerror(cc);
                else *err_out = "HTTP " + std::to_string(status);
            }
            return false;
        }
        std::remove(dest_path.c_str());
        if (std::rename(part_path.c_str(), dest_path.c_str()) != 0) {
            std::remove(part_path.c_str());
            if (err_out) *err_out = "rename failed";
            return false;
        }
        if (final_bytes_out) *final_bytes_out = ctx.written;
        return true;
    }
};

} // namespace jf
