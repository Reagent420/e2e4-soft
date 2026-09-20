#include "simple_json.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace gno {
namespace json {

Value Value::makeBool(bool v) {
    Value out;
    out.type_ = Type::Bool;
    out.bool_ = v;
    return out;
}

Value Value::makeNumber(double v) {
    Value out;
    out.type_ = Type::Number;
    out.num_ = v;
    return out;
}

Value Value::makeString(std::string v) {
    Value out;
    out.type_ = Type::String;
    out.str_ = std::move(v);
    return out;
}

Value Value::makeArray(std::vector<Value> v) {
    Value out;
    out.type_ = Type::Array;
    out.arr_ = std::move(v);
    return out;
}

Value Value::makeObject(std::vector<std::pair<std::string, Value>> v) {
    Value out;
    out.type_ = Type::Object;
    out.obj_ = std::move(v);
    return out;
}

namespace {

// Ограничение глубины: ответ приходит из сети, и «[[[[[[...» не должен
// уронить стек рекурсией. Настоящие ответы GitHub укладываются в 10 уровней.
constexpr int kMaxDepth = 64;

class Parser {
public:
    Parser(const std::string& text) : s_(text) {}

    bool run(Value& out, std::string& error) {
        skipWhitespace();
        if (!parseValue(out, 0)) {
            error = error_;
            return false;
        }
        skipWhitespace();
        if (pos_ != s_.size()) {
            error = "лишние данные после значения (позиция " + std::to_string(pos_) + ")";
            return false;
        }
        return true;
    }

private:
    const std::string& s_;
    std::size_t pos_ = 0;
    std::string error_;

    bool fail(const std::string& message) {
        if (error_.empty())
            error_ = message + " (позиция " + std::to_string(pos_) + ")";
        return false;
    }

    bool eof() const { return pos_ >= s_.size(); }
    char peek() const { return eof() ? '\0' : s_[pos_]; }

    void skipWhitespace() {
        while (!eof()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    bool parseValue(Value& out, int depth) {
        if (depth > kMaxDepth)
            return fail("превышена максимальная глубина вложенности");
        if (eof())
            return fail("неожиданный конец данных");

        switch (peek()) {
            case '{': return parseObject(out, depth);
            case '[': return parseArray(out, depth);
            case '"': {
                std::string str;
                if (!parseString(str)) return false;
                out = Value::makeString(std::move(str));
                return true;
            }
            case 't':
                if (s_.compare(pos_, 4, "true") == 0) {
                    pos_ += 4;
                    out = Value::makeBool(true);
                    return true;
                }
                return fail("ожидалось true");
            case 'f':
                if (s_.compare(pos_, 5, "false") == 0) {
                    pos_ += 5;
                    out = Value::makeBool(false);
                    return true;
                }
                return fail("ожидалось false");
            case 'n':
                if (s_.compare(pos_, 4, "null") == 0) {
                    pos_ += 4;
                    out = Value::makeNull();
                    return true;
                }
                return fail("ожидалось null");
            default:
                return parseNumber(out);
        }
    }

    bool parseObject(Value& out, int depth) {
        ++pos_; // '{'
        std::vector<std::pair<std::string, Value>> fields;
        skipWhitespace();
        if (peek() == '}') {
            ++pos_;
            out = Value::makeObject(std::move(fields));
            return true;
        }
        while (true) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (peek() != ':')
                return fail("ожидалось ':' после ключа");
            ++pos_;
            skipWhitespace();
            Value child;
            if (!parseValue(child, depth + 1)) return false;
            fields.emplace_back(std::move(key), std::move(child));
            skipWhitespace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                out = Value::makeObject(std::move(fields));
                return true;
            }
            return fail("ожидалось ',' или '}' в объекте");
        }
    }

    bool parseArray(Value& out, int depth) {
        ++pos_; // '['
        std::vector<Value> items;
        skipWhitespace();
        if (peek() == ']') {
            ++pos_;
            out = Value::makeArray(std::move(items));
            return true;
        }
        while (true) {
            skipWhitespace();
            Value child;
            if (!parseValue(child, depth + 1)) return false;
            items.push_back(std::move(child));
            skipWhitespace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                out = Value::makeArray(std::move(items));
                return true;
            }
            return fail("ожидалось ',' или ']' в массиве");
        }
    }

    // Дописывает код-точку в UTF-8.
    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7f) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }

    bool parseHex4(std::uint32_t& out) {
        if (pos_ + 4 > s_.size())
            return fail("обрыв \\u-последовательности");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_ + i];
            std::uint32_t digit;
            if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
            else return fail("некорректная \\u-последовательность");
            out = (out << 4) | digit;
        }
        pos_ += 4;
        return true;
    }

    bool parseString(std::string& out) {
        if (peek() != '"')
            return fail("ожидалась строка");
        ++pos_;
        out.clear();
        while (true) {
            if (eof())
                return fail("незакрытая строка");
            const char c = s_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                ++pos_;
                continue;
            }
            // Escape-последовательность.
            ++pos_;
            if (eof())
                return fail("обрыв escape-последовательности");
            const char esc = s_[pos_++];
            switch (esc) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!parseHex4(cp)) return false;
                    // Суррогатная пара: старший + младший.
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (s_.compare(pos_, 2, "\\u") != 0)
                            return fail("ожидалась младшая суррогатная пара");
                        pos_ += 2;
                        std::uint32_t low = 0;
                        if (!parseHex4(low)) return false;
                        if (low < 0xdc00 || low > 0xdfff)
                            return fail("некорректная младшая суррогатная пара");
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                        return fail("одиночная младшая суррогатная пара");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    return fail("неизвестная escape-последовательность");
            }
        }
    }

    bool parseNumber(Value& out) {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (eof() || peek() < '0' || peek() > '9')
            return fail("некорректное число");
        while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        if (peek() == '.') {
            ++pos_;
            if (eof() || peek() < '0' || peek() > '9')
                return fail("некорректная дробная часть");
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (eof() || peek() < '0' || peek() > '9')
                return fail("некорректная экспонента");
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        }

        const std::string token = s_.substr(start, pos_ - start);
        // std::stod бросает исключение на переполнении — в разборе сетевого
        // ответа исключение недопустимо, поэтому ловим и отдаём ошибку.
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size() || errno == ERANGE)
            return fail("число вне диапазона double");
        out = Value::makeNumber(value);
        return true;
    }
};

} // namespace

bool parse(const std::string& text, Value& out, std::string& error) {
    // BOM в начале ответа иногда встречается — без него разбор падает
    // на первом же символе с невнятным сообщением.
    const std::size_t start =
        (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
         static_cast<unsigned char>(text[1]) == 0xBB &&
         static_cast<unsigned char>(text[2]) == 0xBF)
            ? 3
            : 0;
    const std::string body = start == 0 ? text : text.substr(start);
    Parser parser(body);
    return parser.run(out, error);
}

} // namespace json
} // namespace gno
