#pragma once
// files.h
//
// Файловая система гостя поверх файлов хоста.
//
// Пути Windows ("C:\Games\save.dat", "data\\level1.map", "/x/y") сводятся к
// путям хоста: диск C: — корневой каталог гостя (по умолчанию каталог
// EXE). Регистр букв не важен, как в Windows: на хосте с
// регистрозависимой ФС (Linux) нужный файл ищется без учёта регистра.
//
// Открытые файлы — в общей таблице: хендлы KERNEL32 (CreateFile) и
// дескрипторы msvcrt (_open, fopen) ссылаются на одни и те же объекты.

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "handle_table.h"

namespace rt {

class Vfs {
public:
    // Каталог хоста, который гость видит как C:\ .
    void SetRoot(const std::string& hostDir);
    const std::string& Root() const { return root_; }

    // Текущий каталог гостя (Windows-путь с диском, без завершающего "\").
    const std::string& CurrentDirectory() const { return cwd_; }
    bool SetCurrentDirectory(const std::string& guestPath);

    // Полный Windows-путь ("C:\A\b.txt") для относительного/абсолютного пути гостя.
    std::string FullGuestPath(const std::string& guestPath) const;

    // Путь хоста. Существующие компоненты ищутся без учёта регистра;
    // несуществующие (для создания файла) остаются как есть.
    std::string ToHost(const std::string& guestPath) const;

    // Windows-путь для пути хоста внутри корня (для GetFullPathName и т.п.).
    std::string ToGuest(const std::string& hostPath) const;

private:
    std::string root_ = ".";
    std::string cwd_ = "C:";
};

Vfs& FileSystem();

// Открытый файл или устройство.
struct OpenFile : KernelObject {
    OpenFile() : KernelObject(ObjectKind::File) {}
    enum Kind { Disk, ConsoleIn, ConsoleOut, ConsoleErr, Directory, Null } kind = Disk;
    std::FILE* f = nullptr;
    std::string hostPath;
    bool canRead = false, canWrite = false, append = false;
    bool deleteOnClose = false;
    ~OpenFile();
};

class FileTable {
public:
    FileTable();
    // Возвращает хендл KERNEL32 (не 0 и не INVALID_HANDLE_VALUE).
    uint32_t Add(std::shared_ptr<OpenFile> f);
    std::shared_ptr<OpenFile> Get(uint32_t handle) const;
    bool Close(uint32_t handle);
    // Дубликат хендла на тот же объект
    uint32_t Duplicate(uint32_t handle);

    static constexpr uint32_t kStdIn = 0x03, kStdOut = 0x07, kStdErr = 0x0B;

};

FileTable& Files();

// Вывод в консоль гостя (kernel32_module.cpp: g_consoleWriteImpl или stdout).
void ConsoleWrite(bool isStderr, const char* data, size_t len);

} // namespace rt
