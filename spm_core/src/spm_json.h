// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Small, dependency-free JSON parser/serializer (internal).
#ifndef SPM_JSON_H
#define SPM_JSON_H

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spm::json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Type type{Type::Null};
    bool boolean{false};
    double number{0.0};
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    const Value* Find(std::string_view key) const;
    Value* Find(std::string_view key);
    void Set(std::string key, Value value);

    // Convenience accessors that never throw: they return the fallback when
    // the value is missing or has a different type.
    std::string AsString(std::string fallback) const;
    double AsNumber(double fallback) const;
    bool AsBool(bool fallback) const;
};

// Parses a complete JSON document. Returns false and fills `error` with a
// human-readable offset when the input is malformed.
bool Parse(std::string_view text, Value& out, std::string* error = nullptr);

// Serializes a value. indent == 0 writes a compact document.
std::string Serialize(const Value& value, int indent = 2);
std::string Escape(std::string_view text);

} // namespace spm::json

#endif // SPM_JSON_H
