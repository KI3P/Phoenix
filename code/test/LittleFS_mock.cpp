#include "LittleFS_mock.h"
#include <iostream>
#include <cstring>

// Global instances
SDClass SD;

// File class implementation
File::File() : _position(0), _isOpen(false), _isDirectory(false), _dirIteratorValid(false) {}

File::File(const std::string& path, const std::string& mode, std::shared_ptr<std::map<std::string, std::string>> storage, bool isDirectory)
    : _path(path), _mode(mode), _position(0), _isOpen(true), _storage(storage), _isDirectory(isDirectory), _dirIteratorValid(false) {

    if (_storage && _storage->find(_path) != _storage->end()) {
        _content = (*_storage)[_path];
    } else {
        _content = "";
    }

    // If writing mode, start with empty content
    if (_mode == "w" || _mode == "write") {
        _content = "";
    }

    // Initialize directory iterator if this is a directory
    if (_isDirectory && _storage) {
        _dirIterator = _storage->begin();
        _dirIteratorValid = true;
    }
}

File::~File() {
    close();
}

File::operator bool() const {
    return _isOpen;
}

size_t File::write(const char* data, size_t size) {
    if (!_isOpen || !data) return 0;

    _content.append(data, size);
    return size;
}

size_t File::write(const char* data) {
    if (!data) return 0;
    return write(data, strlen(data));
}

size_t File::read(char* buffer, size_t size) {
    if (!_isOpen || !buffer || _position >= _content.size()) return 0;

    size_t available_bytes = _content.size() - _position;
    size_t bytes_to_read = std::min(size, available_bytes);

    memcpy(buffer, _content.c_str() + _position, bytes_to_read);
    _position += bytes_to_read;

    return bytes_to_read;
}

int File::read() {
    if (!_isOpen || _position >= _content.size()) return -1;

    return static_cast<unsigned char>(_content[_position++]);
}

size_t File::size() {
    return _content.size();
}

void File::close() {
    if (_isOpen && _storage && (_mode == "w" || _mode == "write")) {
        (*_storage)[_path] = _content;
    }
    _isOpen = false;
}

bool File::seek(size_t pos) {
    if (!_isOpen) return false;

    _position = std::min(pos, _content.size());
    return true;
}

size_t File::position() {
    return _position;
}

bool File::available() {
    return _isOpen && _position < _content.size();
}

void File::flush() {
    if (_isOpen && _storage && (_mode == "w" || _mode == "write")) {
        (*_storage)[_path] = _content;
    }
}

size_t File::print(const char* str) {
    if (!str) return 0;
    return write(str);
}

size_t File::print(const std::string& str) {
    return write(str.c_str(), str.length());
}

size_t File::println(const char* str) {
    size_t written = print(str);
    written += write("\n", 1);
    return written;
}

// Directory iteration methods
bool File::isDirectory() const {
    return _isDirectory;
}

File File::openNextFile() {
    if (!_isDirectory || !_storage || !_dirIteratorValid) {
        return File(); // Return invalid file
    }

    // Find next file that matches our directory path prefix
    std::string dirPrefix = _path;
    if (!dirPrefix.empty() && dirPrefix.back() != '/') {
        dirPrefix += "/";
    }

    while (_dirIterator != _storage->end()) {
        std::string currentPath = _dirIterator->first;
        ++_dirIterator;

        // Check if this file is in our directory
        if (currentPath.find(dirPrefix) == 0 && currentPath != _path) {
            // Extract the relative path within this directory
            std::string relativePath = currentPath.substr(dirPrefix.length());

            // Check if this is a direct child (no more slashes)
            if (relativePath.find('/') == std::string::npos) {
                // It's a file in this directory
                return File(currentPath, "r", _storage, false);
            }
        }
    }

    // No more files
    return File();
}

const char* File::name() const {
    if (_path.empty()) {
        static const char* empty = "";
        return empty;
    }

    // Find the last slash and return everything after it
    size_t lastSlash = _path.find_last_of('/');
    if (lastSlash != std::string::npos) {
        // Create a static string to return (this is a simplification for the mock)
        static std::string filename = _path.substr(lastSlash + 1);
        return filename.c_str();
    }

    // No slash found, return the whole path
    static std::string filename = _path;
    return filename.c_str();
}

// LittleFS_Program class implementation
LittleFS_Program::LittleFS_Program() : _initialized(false) {
    _storage = std::make_shared<std::map<std::string, std::string>>();
}

LittleFS_Program::~LittleFS_Program() {
    end();
}

bool LittleFS_Program::begin(size_t size) {
    _initialized = true;
    return true;
}

void LittleFS_Program::end() {
    _initialized = false;
    if (_storage) {
        _storage->clear();
    }
}

File LittleFS_Program::open(const char* path, const char* mode) {
    if (!_initialized || !path) {
        return File();
    }

    std::string mode_str = mode ? mode : "r";
    std::string path_str = std::string(path);

    // Check if this should be treated as a directory
    // In our mock, we'll assume it's a directory if the path ends with "/" or if it's a known directory pattern
    bool isDirectory = false;
    if (!path_str.empty() && (path_str.back() == '/' || path_str == "/")) {
        isDirectory = true;
    } else {
        // Check if there are files with this path as prefix (making it a directory)
        for (const auto& entry : *_storage) {
            if (entry.first.find(path_str) == 0 && entry.first != path_str) {
                isDirectory = true;
                break;
            }
        }
    }

    return File(path_str, mode_str, _storage, isDirectory);
}

File LittleFS_Program::open(const char* path, int mode) {
    if (!_initialized || !path) {
        return File();
    }

    std::string mode_str = (mode == FILE_WRITE) ? "w" : "r";
    std::string path_str = std::string(path);

    // Check if this should be treated as a directory
    bool isDirectory = false;
    if (!path_str.empty() && (path_str.back() == '/' || path_str == "/")) {
        isDirectory = true;
    } else {
        // Check if there are files with this path as prefix (making it a directory)
        for (const auto& entry : *_storage) {
            if (entry.first.find(path_str) == 0 && entry.first != path_str) {
                isDirectory = true;
                break;
            }
        }
    }

    return File(path_str, mode_str, _storage, isDirectory);
}

bool LittleFS_Program::exists(const char* path) {
    if (!_initialized || !path || !_storage) return false;

    return _storage->find(std::string(path)) != _storage->end();
}

bool LittleFS_Program::remove(const char* path) {
    if (!_initialized || !path || !_storage) return false;

    auto it = _storage->find(std::string(path));
    if (it != _storage->end()) {
        _storage->erase(it);
        return true;
    }
    return false;
}

bool LittleFS_Program::mkdir(const char* path) {
    // Mock implementation - just return true
    return _initialized;
}

bool LittleFS_Program::rmdir(const char* path) {
    // Mock implementation - just return true
    return _initialized;
}

void LittleFS_Program::clearStorage() {
    if (_storage) {
        _storage->clear();
    }
}

void LittleFS_Program::setFileContent(const std::string& path, const std::string& content) {
    if (_storage) {
        (*_storage)[path] = content;
    }
}

std::string LittleFS_Program::getFileContent(const std::string& path) {
    if (_storage && _storage->find(path) != _storage->end()) {
        return (*_storage)[path];
    }
    return "";
}

// SDClass implementation
SDClass::SDClass() : _initialized(false) {
    _storage = std::make_shared<std::map<std::string, std::string>>();
}

SDClass::~SDClass() {
    end();
}

bool SDClass::begin(uint8_t csPin) {
    _initialized = true;
    return true;
}

void SDClass::end() {
    _initialized = false;
    if (_storage) {
        _storage->clear();
    }
}

File SDClass::open(const char* path, uint8_t mode) {
    if (!_initialized || !path) {
        return File();
    }

    std::string mode_str = (mode == FILE_WRITE) ? "w" : "r";
    return File(std::string(path), mode_str, _storage);
}

bool SDClass::exists(const char* path) {
    if (!_initialized || !path || !_storage) return false;

    return _storage->find(std::string(path)) != _storage->end();
}

bool SDClass::remove(const char* path) {
    if (!_initialized || !path || !_storage) return false;

    auto it = _storage->find(std::string(path));
    if (it != _storage->end()) {
        _storage->erase(it);
        return true;
    }
    return false;
}

bool SDClass::mkdir(const char* path) {
    return _initialized;
}

bool SDClass::rmdir(const char* path) {
    return _initialized;
}

void SDClass::clearStorage() {
    if (_storage) {
        _storage->clear();
    }
}

void SDClass::setFileContent(const std::string& path, const std::string& content) {
    if (_storage) {
        (*_storage)[path] = content;
    }
}

std::string SDClass::getFileContent(const std::string& path) {
    if (_storage && _storage->find(path) != _storage->end()) {
        return (*_storage)[path];
    }
    return "";
}