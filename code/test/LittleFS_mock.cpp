#include "LittleFS_mock.h"
#include <iostream>
#include <cstring>

// Global instances
SDClass SD;

// File class implementation
File::File() : _position(0), _isOpen(false) {}

File::File(const std::string& path, const std::string& mode, std::shared_ptr<std::map<std::string, std::string>> storage)
    : _path(path), _mode(mode), _position(0), _isOpen(true), _storage(storage) {

    if (_storage && _storage->find(_path) != _storage->end()) {
        _content = (*_storage)[_path];
    } else {
        _content = "";
    }

    // If writing mode, start with empty content
    if (_mode == "w" || _mode == "write") {
        _content = "";
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
    return File(std::string(path), mode_str, _storage);
}

File LittleFS_Program::open(const char* path, int mode) {
    if (!_initialized || !path) {
        return File();
    }

    std::string mode_str = (mode == FILE_WRITE) ? "w" : "r";
    return File(std::string(path), mode_str, _storage);
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