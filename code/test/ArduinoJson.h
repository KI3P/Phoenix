#ifndef ARDUINOJSON_H
#define ARDUINOJSON_H

#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <iostream>

// Forward declarations
class JsonDocument;
class JsonVariant;
class JsonObject;
class JsonArray;

// Mock JsonVariant class
class JsonVariant {
public:
    enum Type { TYPE_NULL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING };

    JsonVariant() : _type(TYPE_NULL), _int_value(0), _float_value(0.0), _bool_value(false) {}
    JsonVariant(int value) : _type(TYPE_INT), _int_value(value), _float_value(value), _bool_value(value != 0) {}
    JsonVariant(float value) : _type(TYPE_FLOAT), _int_value((int)value), _float_value(value), _bool_value(value != 0.0) {}
    JsonVariant(bool value) : _type(TYPE_BOOL), _int_value(value ? 1 : 0), _float_value(value ? 1.0 : 0.0), _bool_value(value) {}
    JsonVariant(const char* value) : _type(TYPE_STRING), _int_value(0), _float_value(0.0), _bool_value(false), _string_value(value ? value : "") {}
    JsonVariant(const std::string& value) : _type(TYPE_STRING), _int_value(0), _float_value(0.0), _bool_value(false), _string_value(value) {}

    // Assignment operators
    JsonVariant& operator=(int value) {
        _type = TYPE_INT;
        _int_value = value;
        _float_value = value;
        _bool_value = value != 0;
        return *this;
    }

    JsonVariant& operator=(float value) {
        _type = TYPE_FLOAT;
        _int_value = (int)value;
        _float_value = value;
        _bool_value = value != 0.0;
        return *this;
    }

    JsonVariant& operator=(bool value) {
        _type = TYPE_BOOL;
        _int_value = value ? 1 : 0;
        _float_value = value ? 1.0 : 0.0;
        _bool_value = value;
        return *this;
    }

    JsonVariant& operator=(const char* value) {
        _type = TYPE_STRING;
        _string_value = value ? value : "";
        return *this;
    }

    JsonVariant& operator=(int64_t value) {
        _type = TYPE_INT;
        _int_value = (int)value;
        _float_value = (float)value;
        _bool_value = value != 0;
        return *this;
    }

    JsonVariant& operator=(uint32_t value) {
        _type = TYPE_INT;
        _int_value = (int)value;
        _float_value = (float)value;
        _bool_value = value != 0;
        return *this;
    }

    JsonVariant& operator=(uint8_t value) {
        _type = TYPE_INT;
        _int_value = (int)value;
        _float_value = (float)value;
        _bool_value = value != 0;
        return *this;
    }

    // Default value operators (for missing keys)
    int operator|(int defaultValue) const {
        return (_type == TYPE_NULL) ? defaultValue : _int_value;
    }

    float operator|(float defaultValue) const {
        return (_type == TYPE_NULL) ? defaultValue : _float_value;
    }

    bool operator|(bool defaultValue) const {
        return (_type == TYPE_NULL) ? defaultValue : _bool_value;
    }

    int64_t operator|(int64_t defaultValue) const {
        return (_type == TYPE_NULL) ? defaultValue : (int64_t)_int_value;
    }

    uint32_t operator|(uint32_t defaultValue) const {
        return (_type == TYPE_NULL) ? defaultValue : (uint32_t)_int_value;
    }

    uint8_t operator|(uint8_t defaultValue) const {
        return (_type == TYPE_NULL) ? defaultValue : (uint8_t)_int_value;
    }

    // Conversion operators
    operator int() const { return _int_value; }
    operator float() const { return _float_value; }
    operator bool() const { return _bool_value; }
    operator std::string() const { return _string_value; }

    // Array access
    JsonVariant operator[](int index);
    JsonVariant operator[](const char* key);

    // Type checking
    template<typename T>
    bool is() const { return false; }

    bool isArray() const { return !_array_value.empty(); }
    bool isNull() const { return _type == TYPE_NULL; }

private:
    Type _type;
    int _int_value;
    float _float_value;
    bool _bool_value;
    std::string _string_value;
    std::vector<JsonVariant> _array_value;
    std::map<std::string, JsonVariant> _object_value;

    friend class JsonDocument;
    friend class JsonObject;
    friend class JsonArray;
};

// Mock JsonArray class
class JsonArray {
public:
    JsonArray() {}

    JsonVariant operator[](int index) {
        if (index >= 0 && index < (int)_elements.size()) {
            return _elements[index];
        }
        return JsonVariant();
    }

    void add(const JsonVariant& value) {
        _elements.push_back(value);
    }

    size_t size() const { return _elements.size(); }

private:
    std::vector<JsonVariant> _elements;
};

// Mock JsonObject class
class JsonObject {
public:
    JsonObject() {}

    JsonVariant operator[](const char* key) {
        if (key && _members.find(std::string(key)) != _members.end()) {
            return _members[std::string(key)];
        }
        return JsonVariant();
    }

    JsonVariant& operator[](const std::string& key) {
        return _members[key];
    }

private:
    std::map<std::string, JsonVariant> _members;
};

// Mock JsonDocument class
class JsonDocument {
public:
    JsonDocument() {}

    JsonVariant operator[](const char* key) {
        if (key) {
            auto it = _root.find(std::string(key));
            if (it != _root.end()) {
                return it->second;
            }
        }
        return JsonVariant();
    }

    JsonVariant& operator[](const std::string& key) {
        return _root[key];
    }

    void clear() {
        _root.clear();
    }

    // Friend functions need access to _root
    friend size_t serializeJson(const JsonDocument& doc, std::ostream& output);
    friend size_t serializeJsonPretty(const JsonDocument& doc, std::ostream& output);
    template<typename T> friend class DeserializationError;

private:
    std::map<std::string, JsonVariant> _root;
};

// Mock DeserializationError class
template<typename T = void>
class DeserializationError {
public:
    enum Code { Ok, EmptyInput, IncompleteInput, InvalidInput, NoMemory, TooDeep };

    DeserializationError(Code code = Ok) : _code(code) {}

    operator bool() const { return _code != Ok; }
    Code code() const { return _code; }
    const char* c_str() const {
        switch (_code) {
            case Ok: return "Ok";
            case EmptyInput: return "EmptyInput";
            case IncompleteInput: return "IncompleteInput";
            case InvalidInput: return "InvalidInput";
            case NoMemory: return "NoMemory";
            case TooDeep: return "TooDeep";
            default: return "Unknown";
        }
    }

private:
    Code _code;
};

// Inline implementations for JsonVariant methods that need JsonArray
inline JsonVariant JsonVariant::operator[](int index) {
    if (_array_value.size() > (size_t)index && index >= 0) {
        return _array_value[index];
    }
    return JsonVariant();
}

inline JsonVariant JsonVariant::operator[](const char* key) {
    if (key) {
        auto it = _object_value.find(std::string(key));
        if (it != _object_value.end()) {
            return it->second;
        }
    }
    return JsonVariant();
}

// Template specialization for JsonArray
template<>
inline bool JsonVariant::is<JsonArray>() const {
    return !_array_value.empty();
}

// Function declarations

size_t serializeJson(const JsonDocument& doc, std::ostream& output);
size_t serializeJsonPretty(const JsonDocument& doc, std::ostream& output);

// File interface compatibility
template<typename FileType>
size_t serializeJson(const JsonDocument& doc, FileType& file) {
    std::ostringstream oss;
    size_t bytes = serializeJson(doc, oss);
    std::string str = oss.str();
    file.write(str.c_str(), str.length());
    return bytes;
}

template<typename FileType>
size_t serializeJsonPretty(const JsonDocument& doc, FileType& file) {
    std::ostringstream oss;
    size_t bytes = serializeJsonPretty(doc, oss);
    std::string str = oss.str();
    file.write(str.c_str(), str.length());
    return bytes;
}

template<typename FileType>
DeserializationError<> deserializeJson(JsonDocument& doc, FileType& file) {
    // Mock implementation - just return Ok for now
    return DeserializationError<>(DeserializationError<>::Ok);
}

#endif // ARDUINOJSON_H