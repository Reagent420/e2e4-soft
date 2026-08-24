#include "remediation/backup_store.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace gno {
namespace remediation {

namespace {

// ---------------------------------------------------------------- mini JSON

void escapeInto(const std::string& s, std::string& out) {
    out += '"';
    for (char raw : s) {
        unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += raw;
                }
        }
    }
    out += '"';
}

struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    const Json* find(const std::string& key) const {
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }
    std::string str(const std::string& key, const std::string& fallback = {}) const {
        auto* v = find(key);
        return v && v->kind == Kind::String ? v->text : fallback;
    }
    bool booleanOr(const std::string& key, bool fallback) const {
        auto* v = find(key);
        return v && v->kind == Kind::Bool ? v->boolean : fallback;
    }
    std::uint32_t uint32Or(const std::string& key, std::uint32_t fallback) const {
        auto* v = find(key);
        return v && v->kind == Kind::Number ? static_cast<std::uint32_t>(v->number) : fallback;
    }
    std::int32_t int32Or(const std::string& key, std::int32_t fallback) const {
        auto* v = find(key);
        return v && v->kind == Kind::Number ? static_cast<std::int32_t>(v->number) : fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : s_(src) {}

    bool parse(Json& out) {
        skipWs();
        if (!parseValue(out)) return false;
        skipWs();
        return pos_ >= s_.size();
    }

private:
    void skipWs() { while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) ++pos_; }

    bool parseValue(Json& out) {
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') { out.kind = Json::Kind::String; return parseString(out.text); }
        if (c == 't' || c == 'f') { out.kind = Json::Kind::Bool; return parseBool(out.boolean); }
        if (c == 'n') { out.kind = Json::Kind::Null; return parseLiteral("null"); }
        out.kind = Json::Kind::Number;
        return parseNumber(out.number);
    }

    bool parseBool(bool& out) {
        if (s_.compare(pos_, 4, "true") == 0) { out = true; pos_ += 4; return true; }
        if (s_.compare(pos_, 5, "false") == 0) { out = false; pos_ += 5; return true; }
        return false;
    }
    bool parseLiteral(const char* lit) {
        const std::size_t n = std::char_traits<char>::length(lit);
        if (s_.compare(pos_, n, lit) != 0) return false;
        pos_ += n;
        return true;
    }
    bool parseNumber(double& out) {
        std::size_t start = pos_;
        while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) ||
               s_[pos_] == '-' || s_[pos_] == '+' || s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E')) ++pos_;
        if (pos_ == start) return false;
        out = std::stod(s_.substr(start, pos_ - start));
        return true;
    }
    bool parseString(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                char e = s_[pos_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (pos_ + 4 > s_.size()) return false;
                        unsigned code = std::stoul(s_.substr(pos_, 4), nullptr, 16);
                        pos_ += 4;
                        // encode as UTF-8 (BMP only - enough for our own payloads)
                        if (code < 0x80) {
                            out += static_cast<char>(code);
                        } else if (code < 0x800) {
                            out += static_cast<char>(0xC0 | (code >> 6));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (code >> 12));
                            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: return false;
                }
            } else {
                out += c;
            }
        }
        return false;
    }
    bool parseArray(Json& out) {
        out.kind = Json::Kind::Array;
        ++pos_;
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            skipWs();
            Json item;
            if (!parseValue(item)) return false;
            out.array.push_back(std::move(item));
            skipWs();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
    }
    bool parseObject(Json& out) {
        out.kind = Json::Kind::Object;
        ++pos_;
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            skipWs();
            Json value;
            if (!parseValue(value)) return false;
            out.object.emplace(std::move(key), std::move(value));
            skipWs();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
    }

    const std::string& s_;
    std::size_t pos_ = 0;
};

// ---------------------------------------------------------------- serialization

void writeRegistryData(const RegistryData& r, std::string& out) {
    out += "{\"existed\":" + std::string(r.existed ? "true" : "false");
    out += ",\"value\":" + std::to_string(r.value);
    out += ",\"key_existed\":" + std::string(r.key_existed ? "true" : "false") + "}";
}

RegistryData readRegistryData(const Json& j) {
    RegistryData r;
    r.existed = j.booleanOr("existed", false);
    r.value = j.uint32Or("value", 0);
    r.key_existed = j.booleanOr("key_existed", false);
    return r;
}

void writeValue(const ActionValue& value, std::string& out) {
    struct Writer {
        std::string& out;
        void operator()(const NoneValue&) const { out += "{\"$type\":\"none\"}"; }
        void operator()(const DnsValue& v) const {
            out += "{\"$type\":\"dns\",\"automatic\":";
            out += v.automatic ? "true" : "false";
            out += ",\"servers\":[";
            for (std::size_t i = 0; i < v.servers.size(); ++i) {
                if (i) out += ',';
                escapeInto(v.servers[i], out);
            }
            out += "]}";
        }
        void operator()(const MtuValue& v) const {
            out += "{\"$type\":\"mtu\",\"bytes\":" + std::to_string(v.bytes) + "}";
        }
        void operator()(const TcpValue& v) const {
            out += "{\"$type\":\"tcp\",\"settings\":[";
            for (std::size_t i = 0; i < v.settings.size(); ++i) {
                if (i) out += ',';
                out += "{\"parameter\":" + std::to_string(static_cast<int>(v.settings[i].parameter));
                out += ",\"existed\":" + std::string(v.settings[i].existed ? "true" : "false");
                out += ",\"value\":" + std::to_string(v.settings[i].value) + "}";
            }
            out += "]}";
        }
        void operator()(const PowerPlanValue& v) const {
            out += "{\"$type\":\"power_plan\",\"identifier\":";
            escapeInto(v.identifier, out);
            out += "}";
        }
        void operator()(const GameDvrValue& v) const {
            out += "{\"$type\":\"game_dvr\",\"game_dvr_enabled\":";
            writeRegistryData(v.game_dvr_enabled, out);
            out += ",\"app_capture_enabled\":";
            writeRegistryData(v.app_capture_enabled, out);
            out += "}";
        }
        void operator()(const FullscreenValue& v) const {
            out += "{\"$type\":\"fullscreen\",\"existed\":" + std::string(v.existed ? "true" : "false");
            out += ",\"compatibility_flags\":";
            escapeInto(v.compatibility_flags, out);
            out += ",\"key_existed\":" + std::string(v.key_existed ? "true" : "false") + "}";
        }
        void operator()(const PriorityValue& v) const {
            out += "{\"$type\":\"priority\",\"level\":" + std::to_string(static_cast<int>(v.level)) + "}";
        }
        void operator()(const Cs2MaxPingValue& v) const {
            out += "{\"$type\":\"cs2maxping\",\"max_ping\":" + std::to_string(v.max_ping) + "}";
        }
        void operator()(const RegistryData& v) const {
            out += "{\"$type\":\"registrydata\",\"value\":";
            writeRegistryData(v, out);
            out += "}";
        }
        void operator()(const MouseAccelValue& v) const {
            out += "{\"$type\":\"mouseaccel\",\"speed\":" + std::to_string(v.speed) +
                   ",\"t1\":" + std::to_string(v.threshold1) +
                   ",\"t2\":" + std::to_string(v.threshold2) + "}";
        }
    };
    std::visit(Writer{out}, value);
}

ActionValue readValue(const Json& j) {
    const std::string type = j.str("$type");
    if (type == "dns") {
        DnsValue v;
        v.automatic = j.booleanOr("automatic", true);
        if (auto* arr = j.find("servers"); arr && arr->kind == Json::Kind::Array)
            for (const auto& s : arr->array)
                if (s.kind == Json::Kind::String) v.servers.push_back(s.text);
        return v;
    }
    if (type == "mtu") return MtuValue{j.uint32Or("bytes", 1500)};
    if (type == "tcp") {
        TcpValue v;
        if (auto* arr = j.find("settings"); arr && arr->kind == Json::Kind::Array)
            for (const auto& s : arr->array)
                v.settings.push_back(TcpSetting{
                    static_cast<TcpParameter>(s.int32Or("parameter", 0)),
                    s.booleanOr("existed", false),
                    s.int32Or("value", 0)});
        return v;
    }
    if (type == "power_plan") return PowerPlanValue{j.str("identifier")};
    if (type == "game_dvr") {
        GameDvrValue v;
        if (auto* a = j.find("game_dvr_enabled")) v.game_dvr_enabled = readRegistryData(*a);
        if (auto* b = j.find("app_capture_enabled")) v.app_capture_enabled = readRegistryData(*b);
        return v;
    }
    if (type == "fullscreen")
        return FullscreenValue{j.booleanOr("existed", false), j.str("compatibility_flags"),
                               j.booleanOr("key_existed", false)};
    if (type == "priority") return PriorityValue{static_cast<PriorityLevel>(j.int32Or("level", 0))};
    if (type == "cs2maxping") return Cs2MaxPingValue{j.uint32Or("max_ping", 60)};
    if (type == "registrydata") {
        const Json* inner = j.find("value");
        return inner ? readRegistryData(*inner) : RegistryData{};
    }
    if (type == "mouseaccel")
        return MouseAccelValue{j.uint32Or("speed", 0), j.uint32Or("t1", 0), j.uint32Or("t2", 0)};
    return NoneValue{};
}

void writeState(const ActionState& state, std::string& out) {
    out += "{\"id\":" + std::to_string(static_cast<int>(state.id));
    out += ",\"status\":" + std::to_string(static_cast<int>(state.status));
    out += ",\"value\":";
    writeValue(state.value, out);
    out += ",\"detail\":";
    escapeInto(state.detail, out);
    out += "}";
}

ActionState readState(const Json& j) {
    ActionState s;
    s.id = static_cast<ActionId>(j.int32Or("id", 0));
    s.status = static_cast<ActionStatus>(j.int32Or("status", 0));
    if (auto* v = j.find("value")) s.value = readValue(*v);
    s.detail = j.str("detail");
    return s;
}

void writeTarget(const ActionTarget& target, std::string& out) {
    struct Writer {
        std::string& out;
        void operator()(const NoTarget&) const { out += "{\"$type\":\"none\"}"; }
        void operator()(const InterfaceTarget& t) const {
            out += "{\"$type\":\"interface\",\"id\":";
            escapeInto(t.id, out);
            out += ",\"index\":" + std::to_string(t.index) + "}";
        }
        void operator()(const ExecutableTarget& t) const {
            out += "{\"$type\":\"executable\",\"path\":";
            escapeInto(t.path, out);
            out += "}";
        }
        void operator()(const ProcessTarget& t) const {
            out += "{\"$type\":\"process\",\"pid\":" + std::to_string(t.pid);
            out += ",\"creation_time\":" + std::to_string(t.creation_time);
            out += ",\"path\":";
            escapeInto(t.path, out);
            out += "}";
        }
    };
    std::visit(Writer{out}, target);
}

ActionTarget readTarget(const Json& j) {
    const std::string type = j.str("$type");
    if (type == "interface") return InterfaceTarget{j.str("id"), j.uint32Or("index", 0)};
    if (type == "executable") return ExecutableTarget{j.str("path")};
    if (type == "process")
        return ProcessTarget{j.uint32Or("pid", 0),
                             static_cast<std::uint64_t>(j.find("creation_time") ? j.find("creation_time")->number : 0),
                             j.str("path")};
    return NoTarget{};
}

void writePrepared(const PreparedAction& p, std::string& out) {
    out += "{\"id\":" + std::to_string(static_cast<int>(p.id)) + ",\"target\":";
    writeTarget(p.target, out);
    out += ",\"before\":";
    writeState(p.before, out);
    out += ",\"proposed\":";
    writeState(p.proposed, out);
    out += ",\"safe\":" + std::string(p.safe ? "true" : "false") + "}";
}

PreparedAction readPrepared(const Json& j) {
    PreparedAction p;
    p.id = static_cast<ActionId>(j.int32Or("id", 0));
    if (auto* t = j.find("target")) p.target = readTarget(*t);
    if (auto* b = j.find("before")) p.before = readState(*b);
    if (auto* pr = j.find("proposed")) p.proposed = readState(*pr);
    p.safe = j.booleanOr("safe", false);
    return p;
}

void writeOutcome(const ActionOutcome& o, std::string& out) {
    out += "{\"id\":" + std::to_string(static_cast<int>(o.id));
    out += ",\"status\":" + std::to_string(static_cast<int>(o.status));
    out += ",\"error\":" + std::to_string(static_cast<int>(o.error));
    out += ",\"state\":";
    writeState(o.state, out);
    out += ",\"attempted\":" + std::string(o.attempted ? "true" : "false");
    out += ",\"rollback_attempted\":" + std::string(o.rollback_attempted ? "true" : "false");
    out += ",\"rollback_error\":" + std::to_string(static_cast<int>(o.rollback_error));
    out += ",\"detail\":";
    escapeInto(o.detail, out);
    out += ",\"rollback_detail\":";
    escapeInto(o.rollback_detail, out);
    out += "}";
}

ActionOutcome readOutcome(const Json& j) {
    ActionOutcome o;
    o.id = static_cast<ActionId>(j.int32Or("id", 0));
    o.status = static_cast<ActionStatus>(j.int32Or("status", 0));
    o.error = static_cast<RemediationError>(j.int32Or("error", 0));
    if (auto* s = j.find("state")) o.state = readState(*s);
    o.attempted = j.booleanOr("attempted", false);
    o.rollback_attempted = j.booleanOr("rollback_attempted", false);
    o.rollback_error = static_cast<RemediationError>(j.int32Or("rollback_error", 0));
    o.detail = j.str("detail");
    o.rollback_detail = j.str("rollback_detail");
    return o;
}

std::string serializeRecord(const TransactionRecord& r) {
    std::string out = "{\"schema_version\":" + std::to_string(r.schema_version);
    out += ",\"transaction_id\":";
    escapeInto(r.transaction_id, out);
    out += ",\"status\":" + std::to_string(static_cast<int>(r.status));
    out += ",\"prepared_actions\":[";
    for (std::size_t i = 0; i < r.prepared_actions.size(); ++i) {
        if (i) out += ',';
        writePrepared(r.prepared_actions[i], out);
    }
    out += "],\"outcomes\":[";
    for (std::size_t i = 0; i < r.outcomes.size(); ++i) {
        if (i) out += ',';
        writeOutcome(r.outcomes[i], out);
    }
    out += "],\"action_order\":[";
    for (std::size_t i = 0; i < r.action_order.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(static_cast<int>(r.action_order[i]));
    }
    out += "],\"applied_action_order\":[";
    for (std::size_t i = 0; i < r.applied_action_order.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(static_cast<int>(r.applied_action_order[i]));
    }
    out += "],\"error\":" + std::to_string(static_cast<int>(r.error));
    out += ",\"detail\":";
    escapeInto(r.detail, out);
    out += "}";
    return out;
}

bool parseIntArray(const Json& arr, std::vector<ActionId>& out) {
    for (const auto& item : arr.array) {
        if (item.kind != Json::Kind::Number) return false;
        out.push_back(static_cast<ActionId>(static_cast<int>(item.number)));
    }
    return true;
}

bool deserializeRecord(const std::string& text, TransactionRecord& out) {
    Json root;
    JsonParser parser(text);
    if (!parser.parse(root) || root.kind != Json::Kind::Object) return false;
    out.schema_version = root.uint32Or("schema_version", 1);
    out.transaction_id = root.str("transaction_id");
    out.status = static_cast<TransactionStatus>(root.int32Or("status", 0));
    if (auto* arr = root.find("prepared_actions"); arr && arr->kind == Json::Kind::Array)
        for (const auto& item : arr->array) out.prepared_actions.push_back(readPrepared(item));
    if (auto* arr = root.find("outcomes"); arr && arr->kind == Json::Kind::Array)
        for (const auto& item : arr->array) out.outcomes.push_back(readOutcome(item));
    if (auto* arr = root.find("action_order"); arr && arr->kind == Json::Kind::Array)
        parseIntArray(*arr, out.action_order);
    if (auto* arr = root.find("applied_action_order"); arr && arr->kind == Json::Kind::Array)
        parseIntArray(*arr, out.applied_action_order);
    out.error = static_cast<RemediationError>(root.int32Or("error", 0));
    out.detail = root.str("detail");
    return !out.transaction_id.empty();
}

} // namespace

// ---------------------------------------------------------------- JsonBackupStore

JsonBackupStore::JsonBackupStore(std::string directory) : directory_(std::move(directory)) {
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
}

std::string JsonBackupStore::pathFor(const std::string& transaction_id) const {
    std::string safe;
    for (char c : transaction_id)
        safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return (std::filesystem::path(directory_) / (safe + ".json")).string();
}

SimpleResult JsonBackupStore::save(const TransactionRecord& record) {
    if (record.transaction_id.empty() || record.transaction_id.size() > kMaxTransactionIdLength)
        return Fail(RemediationError::BackupFailed, "invalid transaction id");

    const std::string final_path = pathFor(record.transaction_id);
    const std::string tmp_path = final_path + ".tmp";

    {
        std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
        if (!file) return Fail(RemediationError::BackupFailed, "cannot open temp file: " + tmp_path);
        const std::string payload = serializeRecord(record);
        file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        file.flush();
        if (!file) {
            file.close();
            std::remove(tmp_path.c_str());
            return Fail(RemediationError::BackupFailed, "write failed");
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        // Windows rename fails if destination exists - replace instead.
        std::error_code ec2;
        std::filesystem::remove(final_path, ec2);
        ec.clear();
        std::filesystem::rename(tmp_path, final_path, ec);
        if (ec) {
            std::remove(tmp_path.c_str());
            return Fail(RemediationError::BackupFailed, "rename failed");
        }
    }
    return Ok();
}

Result<TransactionRecord> JsonBackupStore::load(const std::string& transaction_id) {
    std::ifstream file(pathFor(transaction_id), std::ios::binary);
    if (!file) return Fail(RemediationError::InternalFailure, "transaction not found: " + transaction_id);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    TransactionRecord record;
    if (!deserializeRecord(buffer.str(), record))
        return Fail(RemediationError::InternalFailure, "corrupted backup payload");
    return record;
}

Result<std::vector<TransactionSummary>> JsonBackupStore::list() {
    std::vector<TransactionSummary> result;
    std::error_code ec;
    if (!std::filesystem::exists(directory_, ec))
        return result;
    for (const auto& entry : std::filesystem::directory_iterator(directory_, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file) continue;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        TransactionRecord record;
        if (deserializeRecord(buffer.str(), record)) {
            const auto wtime = std::filesystem::last_write_time(entry.path());
            const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(wtime);
            const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
            char time_buf[32] = {};
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", std::localtime(&tt));
            result.push_back({record.transaction_id, record.status, time_buf});
        }
    }
    // Chronological order (newest first). IDs are NOT monotonic.
    std::sort(result.begin(), result.end(),
              [](const TransactionSummary& a, const TransactionSummary& b) {
                  return a.created_at > b.created_at;
              });
    return result;
}

} // namespace remediation
} // namespace gno
