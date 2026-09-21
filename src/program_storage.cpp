#include "program_storage.h"

#include <LittleFS.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "config.h"

static constexpr size_t COPY_BUFFER_SIZE = 16 * 1024;

ProgramStorage::ProgramStorage() : _spi(HSPI) {}

ProgramStorage::~ProgramStorage() {
    end();
}

bool ProgramStorage::validPins(const AppConfig& cfg) const {
    const uint8_t pins[] = {
        cfg.storageSckPin,
        cfg.storageMosiPin,
        cfg.storageMisoPin,
        cfg.storageCsPin,
    };
    for (size_t i = 0; i < sizeof(pins); i++) {
        if (pins[i] > 48) return false;
        for (size_t j = i + 1; j < sizeof(pins); j++) {
            if (pins[i] == pins[j]) return false;
        }
    }

    if (cfg.ledType == LED_TYPE_APA102) {
        for (uint8_t pin : pins) {
            if (pin == cfg.dataPin || pin == cfg.clkPin) return false;
        }
    } else {
        for (uint8_t pin : pins) {
            if (pin == cfg.dataPin) return false;
        }
    }
    return true;
}

bool ProgramStorage::begin(const AppConfig& cfg, bool littleFsMounted) {
    end();
    _littleFsMounted = littleFsMounted;
    _activeFs = &LittleFS;

    if (!validPins(cfg)) {
        LOGLN("[storage] XTSD pin configuration is invalid or conflicts with LED output");
        return ready();
    }

    uint32_t frequency = (uint32_t)cfg.storageSpiFrequencyMhz * 1000000UL;
    _spi.begin(cfg.storageSckPin, cfg.storageMisoPin, cfg.storageMosiPin, cfg.storageCsPin);
    if (!SD.begin(cfg.storageCsPin, _spi, frequency, "/xtsd", 8, false) ||
        SD.cardType() == CARD_NONE || SD.cardSize() < 1024 * 1024) {
        LOGLN("[storage] XTSD not detected; using LittleFS programs");
        SD.end();
        _spi.end();
        return ready();
    }

    _externalDetected = true;
    if (!cfg.externalStorageEnabled) {
        LOG("[storage] XTSD detected: %u MB; disabled for programs\n",
            (unsigned)(SD.cardSize() / (1024 * 1024)));
        SD.end();
        _spi.end();
        return ready();
    }

    _externalMounted = true;
    _activeFs = &SD;
    LOG("[storage] XTSD mounted: %u MB, SPI=%u MHz, SCK=%u MOSI=%u MISO=%u CS=%u\n",
        (unsigned)(SD.cardSize() / (1024 * 1024)),
        (unsigned)cfg.storageSpiFrequencyMhz,
        (unsigned)cfg.storageSckPin,
        (unsigned)cfg.storageMosiPin,
        (unsigned)cfg.storageMisoPin,
        (unsigned)cfg.storageCsPin);
    return true;
}

void ProgramStorage::end() {
    if (_externalMounted) {
        SD.end();
        _spi.end();
    }
    _externalDetected = false;
    _externalMounted = false;
    _activeFs = nullptr;
}

fs::FS& ProgramStorage::fs() {
    return *_activeFs;
}

size_t ProgramStorage::totalBytes() const {
    if (_externalMounted) return SD.totalBytes();
    return _littleFsMounted ? LittleFS.totalBytes() : 0;
}

size_t ProgramStorage::usedBytes() const {
    if (_externalMounted) return SD.usedBytes();
    return _littleFsMounted ? LittleFS.usedBytes() : 0;
}

size_t ProgramStorage::freeBytes() const {
    size_t total = totalBytes();
    size_t used = usedBytes();
    return used < total ? total - used : 0;
}

bool ProgramStorage::isProgramPath(const char* path) const {
    if (!path || !*path) return false;
    const char* ext = strrchr(path, '.');
    return ext && (strcasecmp(ext, ".pix") == 0 || strcasecmp(ext, ".axp") == 0 ||
                   strcasecmp(ext, ".apx") == 0);
}

bool ProgramStorage::copyFile(fs::FS& source, fs::FS& target, const char* path) {
    File input = source.open(path, "r");
    if (!input) return false;
    File output = target.open(path, "w");
    if (!output) {
        input.close();
        return false;
    }

    uint8_t* buffer = (uint8_t*)heap_caps_malloc(COPY_BUFFER_SIZE, MALLOC_CAP_INTERNAL);
    if (!buffer) {
        output.close();
        input.close();
        target.remove(path);
        return false;
    }

    bool ok = true;
    while (input.available()) {
        size_t count = input.read(buffer, COPY_BUFFER_SIZE);
        if (count == 0 || output.write(buffer, count) != count) {
            ok = false;
            break;
        }
        delay(0);
    }
    heap_caps_free(buffer);
    output.close();
    input.close();
    if (!ok) target.remove(path);
    return ok;
}

bool ProgramStorage::migrateProgramsFromLittleFs() {
    if (!_externalMounted || !_littleFsMounted) return true;

    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) return false;
    bool ok = true;
    File entry = root.openNextFile();
    while (entry) {
        String path = entry.name();
        bool copy = !entry.isDirectory() && isProgramPath(path.c_str());
        entry.close();
        if (copy) {
            if (!path.startsWith("/")) path = "/" + path;
            if (SD.exists(path)) {
                entry = root.openNextFile();
                continue;
            }
            if (!copyFile(LittleFS, SD, path.c_str())) {
                LOG("[storage] migration failed: %s\n", path.c_str());
                ok = false;
                break;
            }
            LOG("[storage] migrated program: %s\n", path.c_str());
        }
        entry = root.openNextFile();
    }
    root.close();
    return ok;
}

bool ProgramStorage::erasePrograms() {
    if (!ready()) return false;
    fs::FS& storage = fs();
    File root = storage.open("/");
    if (!root || !root.isDirectory()) return false;
    String paths[64];
    size_t count = 0;
    File entry = root.openNextFile();
    while (entry && count < 64) {
        if (!entry.isDirectory() && isProgramPath(entry.name())) {
            paths[count] = entry.name();
            if (!paths[count].startsWith("/")) paths[count] = "/" + paths[count];
            count++;
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    bool ok = true;
    for (size_t i = 0; i < count; i++) {
        if (!storage.remove(paths[i])) ok = false;
    }
    return ok;
}
