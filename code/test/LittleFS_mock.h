#ifndef LITTLEFS_MOCK_H
#define LITTLEFS_MOCK_H

#include <string>
#include <map>
#include <memory>

// Mock File class to mimic the LittleFS File interface
class File {
public:
    File();
    File(const std::string& path, const std::string& mode, std::shared_ptr<std::map<std::string, std::string>> storage, bool isDirectory = false);
    ~File();

    // File operations
    operator bool() const;
    size_t write(const char* data, size_t size);
    size_t write(const char* data);
    size_t read(char* buffer, size_t size);
    int read();
    size_t size();
    void close();
    bool seek(size_t pos);
    size_t position();
    bool available();
    void flush();

    // Stream operators for ArduinoJson compatibility
    size_t print(const char* str);
    size_t print(const std::string& str);
    size_t println(const char* str);

    // Directory iteration methods
    bool isDirectory() const;
    File openNextFile();
    const char* name() const;

private:
    std::string _path;
    std::string _mode;
    std::string _content;
    size_t _position;
    bool _isOpen;
    std::shared_ptr<std::map<std::string, std::string>> _storage;

    // Directory iteration state
    bool _isDirectory;
    mutable std::map<std::string, std::string>::iterator _dirIterator;
    mutable bool _dirIteratorValid;
};

// Mock LittleFS_Program class
class LittleFS_Program {
public:
    LittleFS_Program();
    ~LittleFS_Program();

    bool begin(size_t size = 0);
    void end();
    File open(const char* path, const char* mode = "r");
    File open(const char* path, int mode);
    bool exists(const char* path);
    bool remove(const char* path);
    bool mkdir(const char* path);
    bool rmdir(const char* path);

    // Test helper methods
    void clearStorage();
    void setFileContent(const std::string& path, const std::string& content);
    std::string getFileContent(const std::string& path);

private:
    std::shared_ptr<std::map<std::string, std::string>> _storage;
    bool _initialized;
};

// Mock SD class
class SDClass {
public:
    SDClass();
    ~SDClass();

    bool begin(uint8_t csPin);
    void end();
    File open(const char* path, uint8_t mode = 0);
    bool exists(const char* path);
    bool remove(const char* path);
    bool mkdir(const char* path);
    bool rmdir(const char* path);

    // Test helper methods
    void clearStorage();
    void setFileContent(const std::string& path, const std::string& content);
    std::string getFileContent(const std::string& path);

private:
    std::shared_ptr<std::map<std::string, std::string>> _storage;
    bool _initialized;
};

// Constants for SD card
#define BUILTIN_SDCARD 254
#define FILE_READ 0
#define FILE_WRITE 1

// Global instances
extern SDClass SD;

#endif // LITTLEFS_MOCK_H