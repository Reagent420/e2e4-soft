#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// Минимальный JSON-парсер для разбора ответа GitHub Releases API
// (core/updater.h). Свой, потому что в проекте нет JSON-библиотеки, а тянуть
// зависимость ради одного запроса не хочется; модуль Qt-free и тестируемый.
//
// Поддерживается весь нужный поднабор RFC 8259: объекты, массивы, строки с
// escape-последовательностями (включая \uXXXX с суррогатными парами), числа,
// true/false/null.
namespace gno {
namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const {
        return type_ == Type::Bool ? bool_ : fallback;
    }
    double asNumber(double fallback = 0.0) const {
        return type_ == Type::Number ? num_ : fallback;
    }
    // Возвращает пустую строку, если значение не строка: вызывающий код
    // проверяет isString(), если пустая строка допустима как данные.
    const std::string& asString() const { return str_; }

    std::size_t size() const {
        if (type_ == Type::Array) return arr_.size();
        if (type_ == Type::Object) return obj_.size();
        return 0;
    }

    // Элемент массива; вне диапазона — Null-значение (не исключение:
    // парсер ответа сети не должен падать из-за чужого формата).
    const Value& at(std::size_t index) const {
        static const Value kNull;
        if (type_ != Type::Array || index >= arr_.size()) return kNull;
        return arr_[index];
    }

    // Поле объекта; отсутствует — Null-значение.
    const Value& operator[](const std::string& key) const {
        static const Value kNull;
        if (type_ != Type::Object) return kNull;
        for (const auto& kv : obj_) {
            if (kv.first == key) return kv.second;
        }
        return kNull;
    }

    bool has(const std::string& key) const {
        return !(*this)[key].isNull();
    }

    const std::vector<Value>& items() const { return arr_; }
    const std::vector<std::pair<std::string, Value>>& fields() const { return obj_; }

    // Конструкторы для тестов и внутреннего использования парсером.
    static Value makeNull() { return Value(); }
    static Value makeBool(bool v);
    static Value makeNumber(double v);
    static Value makeString(std::string v);
    static Value makeArray(std::vector<Value> v);
    static Value makeObject(std::vector<std::pair<std::string, Value>> v);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;
};

// Разбирает текст. При ошибке возвращает false и заполняет error описанием
// с указанием позиции — без этого разбор ответа сервера превращается в гадание.
bool parse(const std::string& text, Value& out, std::string& error);

} // namespace json
} // namespace gno
