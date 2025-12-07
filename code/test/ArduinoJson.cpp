#include "ArduinoJson.h"
#include <sstream>
#include <iomanip>

/**
 * Serialize a JsonVariant value to an output stream
 * Handles all types including nested arrays
 */
void serializeVariant(const JsonVariant& variant, std::ostream& output) {
    switch (variant._type) {
        case JsonVariant::TYPE_NULL:
            output << "null";
            break;
        case JsonVariant::TYPE_BOOL:
            output << (variant._bool_value ? "true" : "false");
            break;
        case JsonVariant::TYPE_INT:
            output << variant._int_value;
            break;
        case JsonVariant::TYPE_FLOAT:
            output << std::fixed << std::setprecision(6) << variant._float_value;
            break;
        case JsonVariant::TYPE_STRING:
            output << "\"" << variant._string_value << "\"";
            break;
        case JsonVariant::TYPE_ARRAY:
            output << "[";
            for (size_t i = 0; i < variant._array_value.size(); i++) {
                if (i > 0) {
                    output << ",";
                }
                serializeVariant(variant._array_value[i], output);
            }
            output << "]";
            break;
    }
}

/**
 * Serialize a JsonDocument to an output stream
 * This is a simple implementation for testing purposes
 */
size_t serializeJson(const JsonDocument& doc, std::ostream& output) {
    std::ostringstream temp;
    bool first = true;

    temp << "{";
    for (const auto& pair : doc._root) {
        if (!first) {
            temp << ",";
        }
        first = false;

        temp << "\"" << pair.first << "\":";
        serializeVariant(pair.second, temp);
    }
    temp << "}";

    std::string result = temp.str();
    output << result;
    return result.length();
}

/**
 * Serialize a JsonVariant value to an output stream with pretty formatting
 * Handles all types including nested arrays
 */
void serializeVariantPretty(const JsonVariant& variant, std::ostream& output, int indent) {
    std::string indentStr(indent, ' ');

    switch (variant._type) {
        case JsonVariant::TYPE_NULL:
            output << "null";
            break;
        case JsonVariant::TYPE_BOOL:
            output << (variant._bool_value ? "true" : "false");
            break;
        case JsonVariant::TYPE_INT:
            output << variant._int_value;
            break;
        case JsonVariant::TYPE_FLOAT:
            output << std::fixed << std::setprecision(6) << variant._float_value;
            break;
        case JsonVariant::TYPE_STRING:
            output << "\"" << variant._string_value << "\"";
            break;
        case JsonVariant::TYPE_ARRAY:
            output << "[\n";
            for (size_t i = 0; i < variant._array_value.size(); i++) {
                output << indentStr << "  ";
                serializeVariantPretty(variant._array_value[i], output, indent + 2);
                if (i < variant._array_value.size() - 1) {
                    output << ",";
                }
                output << "\n";
            }
            output << indentStr << "]";
            break;
    }
}

/**
 * Serialize a JsonDocument to an output stream with pretty formatting
 * This is a simple implementation for testing purposes
 */
size_t serializeJsonPretty(const JsonDocument& doc, std::ostream& output) {
    std::ostringstream temp;
    bool first = true;

    temp << "{\n";
    for (const auto& pair : doc._root) {
        if (!first) {
            temp << ",\n";
        }
        first = false;

        temp << "  \"" << pair.first << "\": ";
        serializeVariantPretty(pair.second, temp, 2);
    }
    temp << "\n}\n";

    std::string result = temp.str();
    output << result;
    return result.length();
}

// JSON Parser implementation

class JsonParser {
public:
    JsonParser(const std::string& json) : _json(json), _pos(0) {}

    DeserializationError<> parse(JsonDocument& doc) {
        doc.clear();
        skipWhitespace();
        if (_pos >= _json.length()) {
            return DeserializationError<>(DeserializationError<>::EmptyInput);
        }
        if (_json[_pos] != '{') {
            return DeserializationError<>(DeserializationError<>::InvalidInput);
        }
        return parseObject(doc);
    }

private:
    const std::string& _json;
    size_t _pos;

    void skipWhitespace() {
        while (_pos < _json.length() &&
               (_json[_pos] == ' ' || _json[_pos] == '\t' ||
                _json[_pos] == '\n' || _json[_pos] == '\r')) {
            _pos++;
        }
    }

    DeserializationError<> parseObject(JsonDocument& doc) {
        if (_json[_pos] != '{') {
            return DeserializationError<>(DeserializationError<>::InvalidInput);
        }
        _pos++; // skip '{'
        skipWhitespace();

        // Handle empty object
        if (_pos < _json.length() && _json[_pos] == '}') {
            _pos++;
            return DeserializationError<>(DeserializationError<>::Ok);
        }

        while (_pos < _json.length()) {
            skipWhitespace();

            // Parse key
            if (_json[_pos] != '"') {
                return DeserializationError<>(DeserializationError<>::InvalidInput);
            }
            std::string key;
            auto err = parseString(key);
            if (err) return err;

            skipWhitespace();
            if (_pos >= _json.length() || _json[_pos] != ':') {
                return DeserializationError<>(DeserializationError<>::InvalidInput);
            }
            _pos++; // skip ':'
            skipWhitespace();

            // Parse value
            err = parseValue(doc[key.c_str()]);
            if (err) return err;

            skipWhitespace();
            if (_pos >= _json.length()) {
                return DeserializationError<>(DeserializationError<>::IncompleteInput);
            }
            if (_json[_pos] == '}') {
                _pos++;
                return DeserializationError<>(DeserializationError<>::Ok);
            }
            if (_json[_pos] != ',') {
                return DeserializationError<>(DeserializationError<>::InvalidInput);
            }
            _pos++; // skip ','
        }
        return DeserializationError<>(DeserializationError<>::IncompleteInput);
    }

    DeserializationError<> parseValue(JsonVariant& variant) {
        skipWhitespace();
        if (_pos >= _json.length()) {
            return DeserializationError<>(DeserializationError<>::IncompleteInput);
        }

        char c = _json[_pos];

        if (c == '"') {
            // String
            std::string str;
            auto err = parseString(str);
            if (err) return err;
            variant = str.c_str();
            return DeserializationError<>(DeserializationError<>::Ok);
        }
        else if (c == '[') {
            // Array
            return parseArray(variant);
        }
        else if (c == '{') {
            // Nested object - not fully supported, but parse and skip
            return parseNestedObject(variant);
        }
        else if (c == 't' || c == 'f') {
            // Boolean
            return parseBoolean(variant);
        }
        else if (c == 'n') {
            // null
            return parseNull(variant);
        }
        else if (c == '-' || (c >= '0' && c <= '9')) {
            // Number
            return parseNumber(variant);
        }

        return DeserializationError<>(DeserializationError<>::InvalidInput);
    }

    DeserializationError<> parseString(std::string& str) {
        if (_json[_pos] != '"') {
            return DeserializationError<>(DeserializationError<>::InvalidInput);
        }
        _pos++; // skip opening quote

        str.clear();
        while (_pos < _json.length() && _json[_pos] != '"') {
            if (_json[_pos] == '\\') {
                _pos++;
                if (_pos >= _json.length()) {
                    return DeserializationError<>(DeserializationError<>::IncompleteInput);
                }
                // Handle escape sequences
                switch (_json[_pos]) {
                    case '"': str += '"'; break;
                    case '\\': str += '\\'; break;
                    case '/': str += '/'; break;
                    case 'b': str += '\b'; break;
                    case 'f': str += '\f'; break;
                    case 'n': str += '\n'; break;
                    case 'r': str += '\r'; break;
                    case 't': str += '\t'; break;
                    default: str += _json[_pos]; break;
                }
            } else {
                str += _json[_pos];
            }
            _pos++;
        }
        if (_pos >= _json.length()) {
            return DeserializationError<>(DeserializationError<>::IncompleteInput);
        }
        _pos++; // skip closing quote
        return DeserializationError<>(DeserializationError<>::Ok);
    }

    DeserializationError<> parseNumber(JsonVariant& variant) {
        size_t start = _pos;
        bool isFloat = false;

        // Handle negative
        if (_json[_pos] == '-') _pos++;

        // Parse integer part
        while (_pos < _json.length() && _json[_pos] >= '0' && _json[_pos] <= '9') {
            _pos++;
        }

        // Check for decimal point
        if (_pos < _json.length() && _json[_pos] == '.') {
            isFloat = true;
            _pos++;
            while (_pos < _json.length() && _json[_pos] >= '0' && _json[_pos] <= '9') {
                _pos++;
            }
        }

        // Check for exponent
        if (_pos < _json.length() && (_json[_pos] == 'e' || _json[_pos] == 'E')) {
            isFloat = true;
            _pos++;
            if (_pos < _json.length() && (_json[_pos] == '+' || _json[_pos] == '-')) {
                _pos++;
            }
            while (_pos < _json.length() && _json[_pos] >= '0' && _json[_pos] <= '9') {
                _pos++;
            }
        }

        std::string numStr = _json.substr(start, _pos - start);
        if (isFloat) {
            variant = (float)std::stod(numStr);
        } else {
            variant = (int)std::stoll(numStr);
        }
        return DeserializationError<>(DeserializationError<>::Ok);
    }

    DeserializationError<> parseBoolean(JsonVariant& variant) {
        if (_json.substr(_pos, 4) == "true") {
            variant = true;
            _pos += 4;
            return DeserializationError<>(DeserializationError<>::Ok);
        }
        if (_json.substr(_pos, 5) == "false") {
            variant = false;
            _pos += 5;
            return DeserializationError<>(DeserializationError<>::Ok);
        }
        return DeserializationError<>(DeserializationError<>::InvalidInput);
    }

    DeserializationError<> parseNull(JsonVariant& variant) {
        if (_json.substr(_pos, 4) == "null") {
            variant = JsonVariant(); // null variant
            _pos += 4;
            return DeserializationError<>(DeserializationError<>::Ok);
        }
        return DeserializationError<>(DeserializationError<>::InvalidInput);
    }

    DeserializationError<> parseArray(JsonVariant& variant) {
        if (_json[_pos] != '[') {
            return DeserializationError<>(DeserializationError<>::InvalidInput);
        }
        _pos++; // skip '['
        skipWhitespace();

        // Initialize as array type
        variant._type = JsonVariant::TYPE_ARRAY;
        variant._array_value.clear();

        // Handle empty array
        if (_pos < _json.length() && _json[_pos] == ']') {
            _pos++;
            return DeserializationError<>(DeserializationError<>::Ok);
        }

        size_t index = 0;
        while (_pos < _json.length()) {
            skipWhitespace();

            // Parse array element
            JsonVariant element;
            auto err = parseValue(element);
            if (err) return err;

            variant._array_value.push_back(element);
            index++;

            skipWhitespace();
            if (_pos >= _json.length()) {
                return DeserializationError<>(DeserializationError<>::IncompleteInput);
            }
            if (_json[_pos] == ']') {
                _pos++;
                return DeserializationError<>(DeserializationError<>::Ok);
            }
            if (_json[_pos] != ',') {
                return DeserializationError<>(DeserializationError<>::InvalidInput);
            }
            _pos++; // skip ','
        }
        return DeserializationError<>(DeserializationError<>::IncompleteInput);
    }

    DeserializationError<> parseNestedObject(JsonVariant& variant) {
        // For nested objects, we just skip them for now
        // A full implementation would recursively parse
        if (_json[_pos] != '{') {
            return DeserializationError<>(DeserializationError<>::InvalidInput);
        }

        int depth = 1;
        _pos++;
        while (_pos < _json.length() && depth > 0) {
            if (_json[_pos] == '{') depth++;
            else if (_json[_pos] == '}') depth--;
            else if (_json[_pos] == '"') {
                // Skip string to avoid counting braces inside strings
                _pos++;
                while (_pos < _json.length() && _json[_pos] != '"') {
                    if (_json[_pos] == '\\') _pos++;
                    _pos++;
                }
            }
            _pos++;
        }
        return DeserializationError<>(DeserializationError<>::Ok);
    }
};

/**
 * Deserialize a JSON string into a JsonDocument
 */
DeserializationError<> deserializeJsonFromString(JsonDocument& doc, const std::string& json) {
    JsonParser parser(json);
    return parser.parse(doc);
}
