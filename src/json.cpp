#include "game_storage/storage.hpp"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace game_storage::detail {
namespace {
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) { ++i; continue; }
        unsigned count = 0;
        std::uint32_t code = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { count = 1; code = lead & 0x1f; }
        else if (lead >= 0xe0 && lead <= 0xef) { count = 2; code = lead & 0x0f; }
        else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; code = lead & 0x07; }
        else return false;
        if (i + count >= text.size()) return false;
        for (unsigned n = 1; n <= count; ++n) {
            const auto tail = static_cast<unsigned char>(text[i + n]);
            if ((tail & 0xc0) != 0x80) return false;
            code = (code << 6) | (tail & 0x3f);
        }
        if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) ||
            (count == 3 && code < 0x10000) || (code >= 0xd800 && code <= 0xdfff) ||
            code > 0x10ffff) return false;
        i += count + 1;
    }
    return true;
}
void append_utf8(std::string& out, std::uint32_t code) {
    if (code <= 0x7f) out += static_cast<char>(code);
    else if (code <= 0x7ff) {
        out += static_cast<char>(0xc0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3f));
    } else if (code <= 0xffff) {
        out += static_cast<char>(0xe0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (code & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (code & 0x3f));
    }
}
bool digit(char ch) { return ch >= '0' && ch <= '9'; }

class Parser {
public:
    explicit Parser(std::string_view source) : source_(source) {}
    Value parse() {
        auto result = value(0);
        whitespace();
        if (position_ != source_.size()) fail();
        return result;
    }
private:
    std::string_view source_;
    std::size_t position_ = 0;
    [[noreturn]] void fail() const { throw std::invalid_argument("Invalid JSON at byte " + std::to_string(position_)); }
    char peek() const { return position_ < source_.size() ? source_[position_] : '\0'; }
    char take() { if (position_ == source_.size()) fail(); return source_[position_++]; }
    void whitespace() {
        while (peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t') ++position_;
    }
    bool accept(char ch) { if (peek() == ch && position_ < source_.size()) { ++position_; return true; } return false; }
    void expect(char ch) { if (!accept(ch)) fail(); }
    void literal(std::string_view text) {
        if (source_.substr(position_, text.size()) != text) fail();
        position_ += text.size();
    }
    std::uint32_t hex4() {
        std::uint32_t result = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = take();
            result <<= 4;
            if (ch >= '0' && ch <= '9') result |= static_cast<unsigned>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') result |= static_cast<unsigned>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') result |= static_cast<unsigned>(ch - 'A' + 10);
            else fail();
        }
        return result;
    }
    std::string string() {
        expect('"');
        std::string out;
        for (;;) {
            const char ch = take();
            if (ch == '"') break;
            if (static_cast<unsigned char>(ch) < 0x20) fail();
            if (ch != '\\') { out += ch; continue; }
            const char escape = take();
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                auto code = hex4();
                if (code >= 0xd800 && code <= 0xdbff) {
                    expect('\\'); expect('u');
                    const auto low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) fail();
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                } else if (code >= 0xdc00 && code <= 0xdfff) fail();
                append_utf8(out, code);
                break;
            }
            default: fail();
            }
        }
        if (!valid_utf8(out)) fail();
        return out;
    }
    Value number() {
        const auto start = position_;
        accept('-');
        if (accept('0')) { if (digit(peek())) fail(); }
        else {
            if (peek() < '1' || peek() > '9') fail();
            do { ++position_; } while (digit(peek()));
        }
        bool floating = false;
        if (accept('.')) {
            floating = true;
            if (!digit(peek())) fail();
            do { ++position_; } while (digit(peek()));
        }
        if (accept('e') || accept('E')) {
            floating = true;
            if (!accept('+')) accept('-');
            if (!digit(peek())) fail();
            do { ++position_; } while (digit(peek()));
        }
        const auto token = source_.substr(start, position_ - start);
        if (!floating) {
            std::int64_t parsed = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) fail();
            return parsed;
        }
        std::istringstream stream{std::string(token)};
        stream.imbue(std::locale::classic());
        double parsed = 0;
        stream >> parsed;
        if (!stream || stream.peek() != std::char_traits<char>::eof() || !std::isfinite(parsed)) fail();
        return parsed;
    }
    Value value(unsigned depth) {
        whitespace();
        if (depth > 128) fail();
        switch (peek()) {
        case 'n': literal("null"); return nullptr;
        case 't': literal("true"); return true;
        case 'f': literal("false"); return false;
        case '"': return string();
        case '[': {
            take(); whitespace();
            Value::Array array;
            if (accept(']')) return array;
            for (;;) {
                array.push_back(value(depth + 1)); whitespace();
                if (accept(']')) break;
                expect(',');
            }
            return array;
        }
        case '{': {
            take(); whitespace();
            Value::Object object;
            if (accept('}')) return object;
            for (;;) {
                whitespace();
                const auto key = string();
                whitespace(); expect(':');
                auto entry = value(depth + 1);
                if (!object.emplace(key, std::move(entry)).second) fail();
                whitespace();
                if (accept('}')) break;
                expect(',');
            }
            return object;
        }
        default: return number();
        }
    }
};

void write_string(std::string& out, std::string_view value) {
    if (!valid_utf8(value)) throw std::invalid_argument("Cannot serialize invalid UTF-8");
    constexpr char hex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 0xf]; }
            else out += static_cast<char>(ch);
        }
    }
    out += '"';
}
void write_value(std::string& out, const Value& value, unsigned depth) {
    if (depth > 128) throw std::invalid_argument("JSON nesting is too deep");
    switch (value.data.index()) {
    case 0: out += "null"; break;
    case 1: out += std::get<bool>(value.data) ? "true" : "false"; break;
    case 2: out += std::to_string(std::get<std::int64_t>(value.data)); break;
    case 3: {
        const auto number = std::get<double>(value.data);
        if (!std::isfinite(number)) throw std::invalid_argument("Cannot serialize non-finite number");
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
        const auto token = stream.str();
        out += token;
        if (token.find_first_of(".eE") == std::string::npos) out += ".0";
        break;
    }
    case 4: write_string(out, std::get<std::string>(value.data)); break;
    case 5: {
        out += '[';
        for (const auto& entry : std::get<Value::Array>(value.data)) {
            if (out.back() != '[') out += ',';
            write_value(out, entry, depth + 1);
        }
        out += ']';
        break;
    }
    case 6: {
        out += '{';
        for (const auto& entry : std::get<Value::Object>(value.data)) {
            if (out.back() != '{') out += ',';
            write_string(out, entry.first);
            out += ':';
            write_value(out, entry.second, depth + 1);
        }
        out += '}';
        break;
    }
    }
}
} // namespace

Value parse_json_value(std::string_view text) { return Parser(text).parse(); }
std::string encode_json_value(const Value& value) {
    std::string result;
    write_value(result, value, 0);
    return result;
}
} // namespace game_storage::detail
