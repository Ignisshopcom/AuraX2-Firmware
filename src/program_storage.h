#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPI.h>

#include "app_config.h"

class ProgramStorage {
public:
    ProgramStorage();
    ~ProgramStorage();

    // LittleFS remains the system/config filesystem. When the optional XTSD
    // module mounts successfully, only program files are redirected to it.
    bool begin(const AppConfig& cfg, bool littleFsMounted);
    void end();

    fs::FS& fs();
    bool ready() const { return _externalMounted || _littleFsMounted; }
    bool externalDetected() const { return _externalDetected; }
    bool externalMounted() const { return _externalMounted; }
    const char* typeName() const { return _externalMounted ? "XTSD" : "LittleFS"; }
    size_t totalBytes() const;
    size_t usedBytes() const;
    size_t freeBytes() const;

    // On the first successful XTSD boot, preserve programs already stored in
    // LittleFS. Existing XTSD content is never overwritten.
    bool migrateProgramsFromLittleFs();

    // Erases program files only. System settings always stay in LittleFS.
    bool erasePrograms();

private:
    bool validPins(const AppConfig& cfg) const;
    bool isProgramPath(const char* path) const;
    bool copyFile(fs::FS& source, fs::FS& target, const char* path);

    SPIClass _spi;
    fs::FS* _activeFs = nullptr;
    bool _littleFsMounted = false;
    bool _externalDetected = false;
    bool _externalMounted = false;
};
