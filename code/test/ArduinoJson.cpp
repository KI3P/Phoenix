#include "ArduinoJson.h"
#include <sstream>
#include <iomanip>

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

        // Handle different value types
        const JsonVariant& variant = pair.second;
        switch (variant._type) {
            case JsonVariant::TYPE_NULL:
                temp << "null";
                break;
            case JsonVariant::TYPE_BOOL:
                temp << (variant._bool_value ? "true" : "false");
                break;
            case JsonVariant::TYPE_INT:
                temp << variant._int_value;
                break;
            case JsonVariant::TYPE_FLOAT:
                temp << std::fixed << std::setprecision(6) << variant._float_value;
                break;
            case JsonVariant::TYPE_STRING:
                temp << "\"" << variant._string_value << "\"";
                break;
        }

        // Handle arrays
        if (variant.isArray()) {
            temp << "[";
            bool firstElement = true;
            for (size_t i = 0; i < variant._array_value.size(); i++) {
                if (!firstElement) {
                    temp << ",";
                }
                firstElement = false;

                const JsonVariant& element = variant._array_value[i];
                switch (element._type) {
                    case JsonVariant::TYPE_NULL:
                        temp << "null";
                        break;
                    case JsonVariant::TYPE_BOOL:
                        temp << (element._bool_value ? "true" : "false");
                        break;
                    case JsonVariant::TYPE_INT:
                        temp << element._int_value;
                        break;
                    case JsonVariant::TYPE_FLOAT:
                        temp << std::fixed << std::setprecision(6) << element._float_value;
                        break;
                    case JsonVariant::TYPE_STRING:
                        temp << "\"" << element._string_value << "\"";
                        break;
                }
            }
            temp << "]";
        }
    }
    temp << "}";

    std::string result = temp.str();
    output << result;
    return result.length();
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

        // Handle different value types
        const JsonVariant& variant = pair.second;
        switch (variant._type) {
            case JsonVariant::TYPE_NULL:
                temp << "null";
                break;
            case JsonVariant::TYPE_BOOL:
                temp << (variant._bool_value ? "true" : "false");
                break;
            case JsonVariant::TYPE_INT:
                temp << variant._int_value;
                break;
            case JsonVariant::TYPE_FLOAT:
                temp << std::fixed << std::setprecision(6) << variant._float_value;
                break;
            case JsonVariant::TYPE_STRING:
                temp << "\"" << variant._string_value << "\"";
                break;
        }

        // Handle arrays
        if (variant.isArray()) {
            temp << "[\n";
            bool firstElement = true;
            for (size_t i = 0; i < variant._array_value.size(); i++) {
                if (!firstElement) {
                    temp << ",\n";
                }
                firstElement = false;
                temp << "    ";

                const JsonVariant& element = variant._array_value[i];
                switch (element._type) {
                    case JsonVariant::TYPE_NULL:
                        temp << "null";
                        break;
                    case JsonVariant::TYPE_BOOL:
                        temp << (element._bool_value ? "true" : "false");
                        break;
                    case JsonVariant::TYPE_INT:
                        temp << element._int_value;
                        break;
                    case JsonVariant::TYPE_FLOAT:
                        temp << std::fixed << std::setprecision(6) << element._float_value;
                        break;
                    case JsonVariant::TYPE_STRING:
                        temp << "\"" << element._string_value << "\"";
                        break;
                }
            }
            temp << "\n  ]";
        }
    }
    temp << "\n}\n";

    std::string result = temp.str();
    output << result;
    return result.length();
}
