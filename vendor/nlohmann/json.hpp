// vendor/nlohmann/json.hpp
// Minimal JSON implementation for the Seating Chart project.
// Implements the nlohmann::json subset actually used here.
// This file is self-contained — zero external dependencies.
// Public-domain / CC0.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nlohmann {

class json {
public:
    // ---------- types ----------
    using array_t  = std::vector<json>;
    // Objects preserve insertion order via vector-of-pairs.
    using object_t = std::vector<std::pair<std::string, json>>;

    enum class value_t { null, boolean, number_integer, string, array, object };

    // ---------- constructors ----------
    json() noexcept                  : t_(value_t::null), i_(0) {}
    json(std::nullptr_t) noexcept    : t_(value_t::null), i_(0) {}
    json(bool v) noexcept            : t_(value_t::boolean), b_(v), i_(0) {}
    json(int v) noexcept             : t_(value_t::number_integer), i_(v) {}
    json(long v) noexcept            : t_(value_t::number_integer), i_(v) {}
    json(long long v) noexcept       : t_(value_t::number_integer), i_(v) {}
    json(unsigned v) noexcept        : t_(value_t::number_integer), i_(static_cast<int64_t>(v)) {}
    json(const char* v)              : t_(value_t::string), s_(v ? v : ""), i_(0) {}
    json(std::string v)              : t_(value_t::string), s_(std::move(v)), i_(0) {}
    json(array_t v)                  : t_(value_t::array),  a_(std::move(v)), i_(0) {}
    json(object_t v)                 : t_(value_t::object), o_(std::move(v)), i_(0) {}

    // Object literal: json{{"k1", v1}, {"k2", v2}}
    // This ctor is chosen when ALL elements can form pair<string,json>
    // and NOT when elements are plain json values (e.g. ints).
    json(std::initializer_list<std::pair<const std::string, json>> init)
        : t_(value_t::object), i_(0)
    { for (const auto& p : init) o_.push_back({p.first, p.second}); }

    static json array()  { json j; j.t_ = value_t::array;  return j; }
    static json object() { json j; j.t_ = value_t::object; return j; }

    // Convenience: array from initializer list of json values
    static json array(std::initializer_list<json> init) {
        json j; j.t_ = value_t::array;
        for (const auto& v : init) j.a_.push_back(v);
        return j;
    }

    // ---------- type checks ----------
    bool is_null()    const noexcept { return t_ == value_t::null; }
    bool is_boolean() const noexcept { return t_ == value_t::boolean; }
    bool is_number()  const noexcept { return t_ == value_t::number_integer; }
    bool is_string()  const noexcept { return t_ == value_t::string; }
    bool is_array()   const noexcept { return t_ == value_t::array; }
    bool is_object()  const noexcept { return t_ == value_t::object; }

    // ---------- size ----------
    size_t size() const noexcept {
        if (t_ == value_t::array)  return a_.size();
        if (t_ == value_t::object) return o_.size();
        return 0;
    }
    bool empty() const noexcept { return size() == 0; }

    // ---------- object access ----------
    json& operator[](const std::string& key) {
        if (t_ == value_t::null) t_ = value_t::object;
        if (t_ != value_t::object)
            throw std::runtime_error("json: operator[string] on non-object");
        for (auto& p : o_) if (p.first == key) return p.second;
        o_.push_back({key, json{}});
        return o_.back().second;
    }

    const json& operator[](const std::string& key) const noexcept {
        static const json null_;
        if (t_ != value_t::object) return null_;
        for (const auto& p : o_) if (p.first == key) return p.second;
        return null_;
    }

    bool contains(const std::string& key) const noexcept {
        if (t_ != value_t::object) return false;
        for (const auto& p : o_) if (p.first == key) return true;
        return false;
    }

    // value(key, default): returns default if key absent or wrong type
    template<typename T>
    T value(const std::string& key, const T& def) const;

    // ---------- array access ----------
    json& operator[](size_t i) { return a_.at(i); }
    const json& operator[](size_t i) const noexcept {
        static const json null_;
        return i < a_.size() ? a_[i] : null_;
    }

    void push_back(json v) {
        if (t_ == value_t::null) t_ = value_t::array;
        if (t_ != value_t::array)
            throw std::runtime_error("json: push_back on non-array");
        a_.push_back(std::move(v));
    }

    // ---------- get<T> ----------
    template<typename T> T get() const;

    // ---------- assignment ----------
    json& operator=(std::nullptr_t) noexcept { t_=value_t::null; return *this; }
    json& operator=(bool v)         noexcept { t_=value_t::boolean; b_=v; return *this; }
    json& operator=(int v)          noexcept { t_=value_t::number_integer; i_=v; return *this; }
    json& operator=(long long v)    noexcept { t_=value_t::number_integer; i_=v; return *this; }
    json& operator=(unsigned v)     noexcept { t_=value_t::number_integer; i_=static_cast<int64_t>(v); return *this; }
    json& operator=(const char* v)           { t_=value_t::string; s_=v?v:""; return *this; }
    json& operator=(std::string v)           { t_=value_t::string; s_=std::move(v); return *this; }
    json& operator=(const json&)   = default;
    json& operator=(json&&)        = default;

    json(const json&) = default;
    json(json&&)      = default;

    // ---------- iteration (arrays) ----------
    array_t::iterator begin() noexcept { return a_.begin(); }
    array_t::iterator end()   noexcept { return a_.end(); }
    array_t::const_iterator begin() const noexcept { return a_.begin(); }
    array_t::const_iterator end()   const noexcept { return a_.end(); }

    // ---------- serialization ----------
    std::string dump(int indent = -1) const;

    // ---------- parsing ----------
    static json parse(const std::string& text);

private:
    value_t     t_;
    bool        b_ = false;
    int64_t     i_;
    std::string s_;
    array_t     a_;
    object_t    o_;

    // ----- dump helpers -----
    static std::string esc_str(const std::string& s);
    void dump_impl(std::string& out, int ind, int depth) const;

    // ----- parser state -----
    struct P {
        const char* cur;
        const char* end_;
        void ws() noexcept {
            while (cur < end_ && (*cur==' '||*cur=='\t'||*cur=='\n'||*cur=='\r')) ++cur;
        }
        char peek() const noexcept { return cur < end_ ? *cur : '\0'; }
        char eat() noexcept { return cur < end_ ? *cur++ : '\0'; }
        bool eat_if(char c) noexcept {
            if (cur < end_ && *cur == c) { ++cur; return true; }
            return false;
        }
        void need(char c) {
            ws();
            if (eat() != c) throw std::runtime_error("json: parse error");
        }
        json val();
        json str_val();
        json num_val();
        json arr_val();
        json obj_val();
    };
};

// ---- value<T> specializations ----

template<> inline std::string json::value<std::string>(
    const std::string& k, const std::string& d) const {
    const auto& v = (*this)[k];
    return v.is_string() ? v.s_ : d;
}
template<> inline int json::value<int>(const std::string& k, const int& d) const {
    const auto& v = (*this)[k];
    return v.is_number() ? static_cast<int>(v.i_) : d;
}
template<> inline bool json::value<bool>(const std::string& k, const bool& d) const {
    const auto& v = (*this)[k];
    return v.is_boolean() ? v.b_ : d;
}
template<> inline json json::value<json>(const std::string& k, const json& d) const {
    if (t_ != value_t::object) return d;
    for (const auto& p : o_) if (p.first == k) return p.second;
    return d;
}

// ---- get<T> specializations ----

template<> inline std::string json::get<std::string>() const {
    if (!is_string()) throw std::runtime_error("json: get<string> on non-string");
    return s_;
}
template<> inline int json::get<int>() const {
    if (!is_number()) throw std::runtime_error("json: get<int> on non-number");
    return static_cast<int>(i_);
}
template<> inline int64_t json::get<int64_t>() const {
    if (!is_number()) throw std::runtime_error("json: get<int64_t> on non-number");
    return i_;
}
template<> inline bool json::get<bool>() const {
    if (!is_boolean()) throw std::runtime_error("json: get<bool> on non-bool");
    return b_;
}

// ---- dump ----

inline std::string json::esc_str(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 2);
    r += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':  r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n";  break;
        case '\r': r += "\\r";  break;
        case '\t': r += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                r += buf;
            } else {
                r += static_cast<char>(c);
            }
        }
    }
    r += '"';
    return r;
}

inline void json::dump_impl(std::string& out, int ind, int depth) const {
    auto nl = [&](int d) {
        if (ind >= 0) { out += '\n'; out.append(static_cast<size_t>(d * ind), ' '); }
    };
    const char* sep = (ind >= 0) ? ": " : ":";
    switch (t_) {
    case value_t::null:            out += "null"; break;
    case value_t::boolean:         out += b_ ? "true" : "false"; break;
    case value_t::number_integer:  out += std::to_string(i_); break;
    case value_t::string:          out += esc_str(s_); break;
    case value_t::array:
        out += '[';
        for (size_t i = 0; i < a_.size(); ++i) {
            if (i) out += ',';
            nl(depth + 1);
            a_[i].dump_impl(out, ind, depth + 1);
        }
        if (!a_.empty()) nl(depth);
        out += ']';
        break;
    case value_t::object:
        out += '{';
        for (size_t i = 0; i < o_.size(); ++i) {
            if (i) out += ',';
            nl(depth + 1);
            out += esc_str(o_[i].first);
            out += sep;
            o_[i].second.dump_impl(out, ind, depth + 1);
        }
        if (!o_.empty()) nl(depth);
        out += '}';
        break;
    }
}

inline std::string json::dump(int indent) const {
    std::string out;
    dump_impl(out, indent, 0);
    return out;
}

// ---- parser ----

inline json json::P::str_val() {
    ws();
    if (eat() != '"') throw std::runtime_error("json: expected '\"'");
    std::string result;
    while (cur < end_ && *cur != '"') {
        if (*cur == '\\') {
            ++cur;
            if (cur >= end_) throw std::runtime_error("json: unexpected end in string");
            switch (*cur) {
            case '"':  result += '"';  break;
            case '\\': result += '\\'; break;
            case '/':  result += '/';  break;
            case 'n':  result += '\n'; break;
            case 'r':  result += '\r'; break;
            case 't':  result += '\t'; break;
            case 'b':  result += '\b'; break;
            case 'f':  result += '\f'; break;
            case 'u': {
                if (end_ - cur < 5)
                    throw std::runtime_error("json: truncated \\uXXXX");
                char h[5] = { cur[1], cur[2], cur[3], cur[4], '\0' };
                unsigned long cp = strtoul(h, nullptr, 16);
                cur += 4;
                if (cp < 0x80) {
                    result += static_cast<char>(cp);
                } else if (cp < 0x800) {
                    result += static_cast<char>(0xC0 | (cp >> 6));
                    result += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    result += static_cast<char>(0xE0 | (cp >> 12));
                    result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    result += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: result += *cur; break;
            }
        } else {
            result += *cur;
        }
        ++cur;
    }
    if (cur >= end_) throw std::runtime_error("json: unterminated string");
    ++cur; // consume closing '"'
    return json(std::move(result));
}

inline json json::P::num_val() {
    ws();
    bool neg = (peek() == '-');
    if (neg) eat();
    if (cur >= end_ || (*cur < '0' || *cur > '9'))
        throw std::runtime_error("json: expected digit");
    int64_t v = 0;
    while (cur < end_ && *cur >= '0' && *cur <= '9') v = v * 10 + (*cur++ - '0');
    // Skip decimal/exponent (truncate to integer for our use case)
    if (peek() == '.') {
        eat();
        while (cur < end_ && *cur >= '0' && *cur <= '9') ++cur;
    }
    if (peek() == 'e' || peek() == 'E') {
        eat();
        if (peek() == '+' || peek() == '-') eat();
        while (cur < end_ && *cur >= '0' && *cur <= '9') ++cur;
    }
    return json(neg ? -v : v);
}

inline json json::P::arr_val() {
    need('[');
    json a = json::array();
    ws();
    if (eat_if(']')) return a;
    do {
        ws();
        a.push_back(val());
        ws();
    } while (eat_if(','));
    need(']');
    return a;
}

inline json json::P::obj_val() {
    need('{');
    json o = json::object();
    ws();
    if (eat_if('}')) return o;
    do {
        ws();
        if (peek() != '"') throw std::runtime_error("json: expected key string");
        std::string key = str_val().get<std::string>();
        need(':');
        ws();
        o[key] = val();
        ws();
    } while (eat_if(','));
    need('}');
    return o;
}

inline json json::P::val() {
    ws();
    switch (peek()) {
    case '"': return str_val();
    case '{': return obj_val();
    case '[': return arr_val();
    case 't':
        if (end_ - cur >= 4 && cur[0]=='t'&&cur[1]=='r'&&cur[2]=='u'&&cur[3]=='e')
            { cur += 4; return json(true); }
        throw std::runtime_error("json: invalid literal");
    case 'f':
        if (end_ - cur >= 5 && cur[0]=='f'&&cur[1]=='a'&&cur[2]=='l'&&cur[3]=='s'&&cur[4]=='e')
            { cur += 5; return json(false); }
        throw std::runtime_error("json: invalid literal");
    case 'n':
        if (end_ - cur >= 4 && cur[0]=='n'&&cur[1]=='u'&&cur[2]=='l'&&cur[3]=='l')
            { cur += 4; return json{}; }
        throw std::runtime_error("json: invalid literal");
    default:
        if (peek() == '-' || (peek() >= '0' && peek() <= '9')) return num_val();
        throw std::runtime_error(std::string("json: unexpected char '") + peek() + '\'');
    }
}

inline json json::parse(const std::string& text) {
    P p{ text.c_str(), text.c_str() + text.size() };
    p.ws();
    json result = p.val();
    p.ws();
    if (p.cur != p.end_)
        throw std::runtime_error("json: trailing characters after value");
    return result;
}

} // namespace nlohmann
