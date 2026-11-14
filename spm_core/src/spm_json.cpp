// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Small, dependency-free JSON parser/serializer (internal).
#include "spm_json.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>
#include <system_error>

namespace spm::json {

const Value* Value::Find(std::string_view key) const {
    for (const auto& entry : object) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

Value* Value::Find(std::string_view key) {
    for (auto& entry : object) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

void Value::Set(std::string key, Value value) {
    for (auto& entry : object) {
        if (entry.first == key) {
            entry.second = std::move(value);
            return;
        }
    }
    object.emplace_back(std::move(key), std::move(value));
}

std::string Value::AsString(std::string fallback) const {
    return IsString() ? string : std::move(fallback);
}

double Value::AsNumber(double fallback) const {
    return IsNumber() ? number : fallback;
}

bool Value::AsBool(bool fallback) const {
    return IsBool() ? boolean : fallback;
}

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    Parser(std::string_view text, std::string* error) : m_text(text), m_error(error) {}

    bool ParseDocument(Value& out) {
        SkipWhitespace();
        if (!ParseValue(out, 0)) {
            return false;
        }
        SkipWhitespace();
        if (m_pos != m_text.size()) {
            return Fail("unexpected trailing data");
        }
        return true;
    }

private:
    bool Fail(const char* message) {
        if (m_error) {
            *m_error = std::string(message) + " at offset " + std::to_string(m_pos);
        }
        return false;
    }

    void SkipWhitespace() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    bool Consume(char expected) {
        if (m_pos < m_text.size() && m_text[m_pos] == expected) {
            ++m_pos;
            return true;
        }
        return false;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > kMaxDepth) {
            return Fail("maximum nesting depth exceeded");
        }
        SkipWhitespace();
        if (m_pos >= m_text.size()) {
            return Fail("unexpected end of input");
        }

        const char c = m_text[m_pos];
        switch (c) {
            case '{':
                return ParseObject(out, depth);
            case '[':
                return ParseArray(out, depth);
            case '"':
                out.type = Type::String;
                return ParseString(out.string);
            case 't':
                out.type = Type::Bool;
                out.boolean = true;
                return ParseLiteral("true");
            case 'f':
                out.type = Type::Bool;
                out.boolean = false;
                return ParseLiteral("false");
            case 'n':
                out.type = Type::Null;
                return ParseLiteral("null");
            default:
                if (c == '-' || (c >= '0' && c <= '9')) {
                    out.type = Type::Number;
                    return ParseNumber(out.number);
                }
                return Fail("unexpected character");
        }
    }

    bool ParseLiteral(std::string_view literal) {
        if (m_text.substr(m_pos, literal.size()) != literal) {
            return Fail("invalid literal");
        }
        m_pos += literal.size();
        return true;
    }

    bool ParseObject(Value& out, int depth) {
        out.type = Type::Object;
        out.object.clear();
        ++m_pos; // '{'
        SkipWhitespace();
        if (Consume('}')) {
            return true;
        }

        for (;;) {
            SkipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
                return Fail("expected an object key");
            }
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (!Consume(':')) {
                return Fail("expected ':' after object key");
            }
            Value value;
            if (!ParseValue(value, depth + 1)) {
                return false;
            }
            out.Set(std::move(key), std::move(value));

            SkipWhitespace();
            if (Consume('}')) {
                return true;
            }
            if (!Consume(',')) {
                return Fail("expected ',' or '}' in object");
            }
        }
    }

    bool ParseArray(Value& out, int depth) {
        out.type = Type::Array;
        out.array.clear();
        ++m_pos; // '['
        SkipWhitespace();
        if (Consume(']')) {
            return true;
        }

        for (;;) {
            Value value;
            if (!ParseValue(value, depth + 1)) {
                return false;
            }
            out.array.push_back(std::move(value));

            SkipWhitespace();
            if (Consume(']')) {
                return true;
            }
            if (!Consume(',')) {
                return Fail("expected ',' or ']' in array");
            }
        }
    }

    bool ParseHex4(uint32_t& value) {
        if (m_pos + 4 > m_text.size()) {
            return Fail("truncated \\u escape");
        }
        uint32_t parsed = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_text[m_pos++];
            parsed <<= 4;
            if (c >= '0' && c <= '9') {
                parsed |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                parsed |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                parsed |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return Fail("invalid hex digit in \\u escape");
            }
        }
        value = parsed;
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t codePoint) {
        if (codePoint <= 0x7F) {
            out.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool ParseString(std::string& out) {
        out.clear();
        if (!Consume('"')) {
            return Fail("expected a string");
        }

        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return Fail("unescaped control character in string");
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }

            if (m_pos >= m_text.size()) {
                return Fail("truncated string escape");
            }
            const char escape = m_text[m_pos++];
            switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t codePoint = 0;
                    if (!ParseHex4(codePoint)) {
                        return false;
                    }
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                        if (!Consume('\\') || !Consume('u')) {
                            return Fail("unpaired high surrogate");
                        }
                        uint32_t low = 0;
                        if (!ParseHex4(low)) {
                            return false;
                        }
                        if (low < 0xDC00 || low > 0xDFFF) {
                            return Fail("invalid low surrogate");
                        }
                        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                    } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                        return Fail("unpaired low surrogate");
                    }
                    AppendUtf8(out, codePoint);
                    break;
                }
                default:
                    return Fail("invalid string escape");
            }
        }
        return Fail("unterminated string");
    }

    bool ParseNumber(double& out) {
        const size_t start = m_pos;
        if (m_pos < m_text.size() && m_text[m_pos] == '-') {
            ++m_pos;
        }
        if (m_pos >= m_text.size()) {
            return Fail("truncated number");
        }

        // Strict JSON grammar. The conversion itself is delegated to
        // std::from_chars so the result is correctly rounded: accumulating
        // fraction digits as value += digit * 0.1 introduced error
        // (0.65 used to come back as 0.65000000000000013).
        if (m_text[m_pos] == '0') {
            ++m_pos;
            if (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                return Fail("leading zero in number");
            }
        } else if (m_text[m_pos] >= '1' && m_text[m_pos] <= '9') {
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                ++m_pos;
            }
        } else {
            return Fail("invalid number");
        }

        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            if (m_pos >= m_text.size() || m_text[m_pos] < '0' || m_text[m_pos] > '9') {
                return Fail("invalid number fraction");
            }
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                ++m_pos;
            }
        }

        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) {
                ++m_pos;
            }
            if (m_pos >= m_text.size() || m_text[m_pos] < '0' || m_text[m_pos] > '9') {
                return Fail("invalid number exponent");
            }
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                ++m_pos;
            }
        }

        double parsed = 0.0;
        const char* first = m_text.data() + start;
        const char* last = m_text.data() + m_pos;
        const auto result = std::from_chars(first, last, parsed, std::chars_format::general);
        if (result.ec != std::errc() || result.ptr != last || !std::isfinite(parsed)) {
            m_pos = start;
            return Fail("number out of range");
        }
        out = parsed;
        return true;
    }

    std::string_view m_text;
    std::string* m_error = nullptr;
    size_t m_pos = 0;
};

void WriteIndent(std::string& out, int indent, int depth) {
    if (indent <= 0) {
        return;
    }
    out.append(static_cast<size_t>(indent * depth), ' ');
}

void WriteString(std::string& out, std::string_view text) {
    out.push_back('"');
    out += Escape(text);
    out.push_back('"');
}

void WriteValue(std::string& out, const Value& value, int indent, int depth) {
    switch (value.type) {
        case Type::Null:
            out += "null";
            break;
        case Type::Bool:
            out += value.boolean ? "true" : "false";
            break;
        case Type::Number: {
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.number);
            if (result.ec == std::errc()) {
                out.append(buffer, result.ptr);
            } else {
                std::ostringstream stream;
                stream.imbue(std::locale::classic());
                stream.precision(17);
                stream << value.number;
                out += stream.str();
            }
            break;
        }
        case Type::String:
            WriteString(out, value.string);
            break;
        case Type::Array: {
            out.push_back('[');
            if (!value.array.empty()) {
                for (size_t i = 0; i < value.array.size(); ++i) {
                    if (i != 0) {
                        out.push_back(',');
                    }
                    if (indent > 0) {
                        out.push_back('\n');
                        WriteIndent(out, indent, depth + 1);
                    }
                    WriteValue(out, value.array[i], indent, depth + 1);
                }
                if (indent > 0) {
                    out.push_back('\n');
                    WriteIndent(out, indent, depth);
                }
            }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            out.push_back('{');
            if (!value.object.empty()) {
                for (size_t i = 0; i < value.object.size(); ++i) {
                    if (i != 0) {
                        out.push_back(',');
                    }
                    if (indent > 0) {
                        out.push_back('\n');
                        WriteIndent(out, indent, depth + 1);
                    }
                    WriteString(out, value.object[i].first);
                    out.push_back(':');
                    if (indent > 0) {
                        out.push_back(' ');
                    }
                    WriteValue(out, value.object[i].second, indent, depth + 1);
                }
                if (indent > 0) {
                    out.push_back('\n');
                    WriteIndent(out, indent, depth);
                }
            }
            out.push_back('}');
            break;
        }
    }
}

} // namespace

bool Parse(std::string_view text, Value& out, std::string* error) {
    out = Value{};
    if (error) {
        error->clear();
    }
    Parser parser(text, error);
    return parser.ParseDocument(out);
}

std::string Serialize(const Value& value, int indent) {
    std::string out;
    WriteValue(out, value, indent, 0);
    return out;
}

std::string Escape(std::string_view text) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(text.size());
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
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
                    out += "\\u00";
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0x0F]);
                } else {
                    out.push_back(raw);
                }
                break;
        }
    }
    return out;
}

} // namespace spm::json
