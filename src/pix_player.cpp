#include "pix_player.h"
#include "config.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <string.h>

// ── PIX format constants ──────────────────────────────────────────────────────

static constexpr uint8_t PIX_LABEL_PROG_PARAM = 0xD1;
static constexpr uint8_t PIX_LABEL_COMMAND    = 0xA1;
static constexpr uint8_t PIX_LABEL_CMD_PARAM  = 0xB1;
static constexpr uint8_t PIX_CMD_PICTURE      = 1;

static constexpr uint32_t AXP_MAGIC           = 0x31505841;  // "AXP1"
static constexpr uint32_t AXP_VERSION         = 1;
static constexpr uint32_t AXP_CODEC_RAW       = 0;
static constexpr uint32_t AXP_CODEC_COLUMNS   = 1;
static constexpr uint32_t AXP_CODEC_LZSS      = 2;
static constexpr uint8_t  AXP_COLUMN_RAW      = 0;
static constexpr uint8_t  AXP_COLUMN_RLE      = 1;
static constexpr uint8_t  AXP_COLUMN_REPEAT   = 2;
static constexpr int      PIX_ERR_LED_COUNT_MISMATCH = 7;

// Program parameter types
static constexpr uint8_t PP_END_BEHAVIOR      = 0x06;

// Command parameter types
static constexpr uint8_t CP_START_TIME        = 0x05;
static constexpr uint8_t CP_END_TIME          = 0x06;
static constexpr uint8_t CP_OFFSET            = 0x04;
static constexpr uint8_t CP_WIDTH             = 0x0A;
static constexpr uint8_t CP_HEIGHT            = 0x0B;
static constexpr uint8_t CP_FREQUENCY         = 0x07;
static constexpr uint8_t CP_LAST_CMD          = 0x0E;

// ── helpers ───────────────────────────────────────────────────────────────────

static bool readDW(File& f, uint32_t& out) {
    uint8_t b[4];
    if (f.read(b, 4) != 4) return false;
    out = (uint32_t)b[0]
        | ((uint32_t)b[1] << 8)
        | ((uint32_t)b[2] << 16)
        | ((uint32_t)b[3] << 24);
    return true;
}

static bool readByte(File& f, uint8_t& out) {
    int v = f.read();
    if (v < 0) return false;
    out = (uint8_t)v;
    return true;
}

static bool readU16(File& f, uint16_t& out) {
    uint8_t b[2];
    if (f.read(b, 2) != 2) return false;
    out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return true;
}

static bool readLimitedByte(File& f, uint32_t& remaining, uint8_t& out) {
    if (remaining == 0) return false;
    int v = f.read();
    if (v < 0) return false;
    remaining--;
    out = (uint8_t)v;
    return true;
}

static bool decodeLzss(File& f, uint32_t encodedSize, uint8_t* dst, size_t decodedSize) {
    uint32_t remaining = encodedSize;
    size_t outPos = 0;

    while (outPos < decodedSize) {
        uint8_t flags = 0;
        if (!readLimitedByte(f, remaining, flags)) return false;

        for (uint8_t bit = 0; bit < 8 && outPos < decodedSize; bit++) {
            if (flags & (1u << bit)) {
                uint8_t lo = 0;
                uint8_t hi = 0;
                if (!readLimitedByte(f, remaining, lo) || !readLimitedByte(f, remaining, hi)) return false;
                uint16_t token = (uint16_t)lo | ((uint16_t)hi << 8);
                size_t offset = (size_t)(token & 0x0FFFu) + 1u;
                size_t length = (size_t)(token >> 12) + 3u;
                if (offset > outPos || outPos + length > decodedSize) return false;
                for (size_t i = 0; i < length; i++) {
                    dst[outPos] = dst[outPos - offset];
                    outPos++;
                }
            } else {
                uint8_t literal = 0;
                if (!readLimitedByte(f, remaining, literal)) return false;
                dst[outPos++] = literal;
            }
            if ((outPos & 0x3FFFu) == 0) delay(0);
        }
    }

    return remaining == 0;
}

static bool decodeLzssBuffer(const uint8_t* src, size_t encodedSize, uint8_t* dst, size_t decodedSize) {
    size_t inPos = 0;
    size_t outPos = 0;

    auto readByte = [&]() -> int {
        if (inPos >= encodedSize) return -1;
        return src[inPos++];
    };

    while (outPos < decodedSize) {
        int flagsIn = readByte();
        if (flagsIn < 0) return false;
        uint8_t flags = (uint8_t)flagsIn;

        for (uint8_t bit = 0; bit < 8 && outPos < decodedSize; bit++) {
            if (flags & (1u << bit)) {
                int loIn = readByte();
                int hiIn = readByte();
                if (loIn < 0 || hiIn < 0) return false;
                uint16_t token = (uint16_t)loIn | ((uint16_t)hiIn << 8);
                size_t offset = (size_t)(token & 0x0FFFu) + 1u;
                size_t length = (size_t)(token >> 12) + 3u;
                if (offset > outPos || outPos + length > decodedSize) return false;
                for (size_t i = 0; i < length; i++) {
                    dst[outPos] = dst[outPos - offset];
                    outPos++;
                }
            } else {
                int literal = readByte();
                if (literal < 0) return false;
                dst[outPos++] = (uint8_t)literal;
            }
            if ((outPos & 0x3FFFu) == 0) delay(0);
        }
    }

    return inPos == encodedSize;
}

static void splitDW(uint32_t dw, uint8_t& label, uint8_t& type, uint16_t& size) {
    label = (dw >> 24) & 0xFF;
    type  = (dw >> 16) & 0xFF;
    size  = dw & 0xFFFF;
}

static uint32_t clampFrequencyForDriver(ILedDriver& leds, uint32_t requested) {
    uint16_t maxHz = leds.maxRefreshHz();
    if (maxHz == 0 || requested <= maxHz) return requested;
    LOG("[pix] frequency capped: %u -> %u Hz for this LED driver\n",
        (unsigned)requested, (unsigned)maxHz);
    return maxHz;
}

static uint8_t* allocDecodedProgramBuffer(size_t bytes) {
    if (bytes == 0) return nullptr;

    uint8_t* buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (buf) return buf;

#if defined(ARDUINO_ARCH_ESP32C3)
    static constexpr size_t INTERNAL_HEAP_RESERVE = 96 * 1024;
#else
    static constexpr size_t INTERNAL_HEAP_RESERVE = 32 * 1024;
#endif
    // ESP32-C3 has no PSRAM. Small/medium compressed AXP programs can still
    // run from internal DRAM if we leave enough room for WiFi and HTTP.
    size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (largestInternal > bytes + INTERNAL_HEAP_RESERVE) {
        buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL);
        if (buf) {
            LOG("[axp] decoded buffer in internal DRAM: %u bytes\n", (unsigned)bytes);
            return buf;
        }
    }

    LOG("[axp] not enough memory for decoded buffer: need %u, largest internal %u\n",
        (unsigned)bytes, (unsigned)largestInternal);
    return nullptr;
}

static uint8_t* allocAxpCommandCache(size_t bytes) {
    if (bytes == 0) return nullptr;

    uint8_t* buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (buf) return buf;

    size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
#if defined(ARDUINO_ARCH_ESP32C3)
    static constexpr size_t INTERNAL_HEAP_RESERVE = 64 * 1024;
#else
    static constexpr size_t INTERNAL_HEAP_RESERVE = 32 * 1024;
#endif
    if (largestInternal <= bytes + INTERNAL_HEAP_RESERVE) {
        LOG("[axp] not enough memory for command cache: need %u, largest internal %u\n",
            (unsigned)bytes, (unsigned)largestInternal);
        return nullptr;
    }
    return (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL);
}

// ── PixPlayer ─────────────────────────────────────────────────────────────────

PixPlayer::PixPlayer(ILedDriver& leds, fs::FS& storage) : _leds(leds), _storage(storage) {}

PixPlayer::~PixPlayer() {
    stopTask();
    unload();
}

void PixPlayer::unload() {
    stopPrefetchTask();
    if (_file) _file.close();
    heap_caps_free(_preloadBuf); _preloadBuf = nullptr;
    for (int i = 0; i < 2; i++) {
        heap_caps_free(_axpCmdCache[i]);
        _axpCmdCache[i] = nullptr;
        _axpCachedCmd[i] = -1;
        _axpCacheLoading[i] = false;
    }
    _axpCmdCacheSize = 0;
    if (_prefetchQueue) {
        vQueueDelete(_prefetchQueue);
        _prefetchQueue = nullptr;
    }
    if (_cacheMutex) {
        vSemaphoreDelete(_cacheMutex);
        _cacheMutex = nullptr;
    }
    free(_colBuf);               _colBuf     = nullptr;
    _preloaded = false;
    _axpStreaming = false;
    _axpConsumerCmd = -1;
    _loaded    = false;
    _format    = ProgramFormat::Pix;
    _numCmds   = 0;
    _curCmd    = 0;
    _curCol    = 0;
    _currentFpsX10 = 0;
    _fpsWindowFrames = 0;
}

// ── parsing ───────────────────────────────────────────────────────────────────

int PixPlayer::parseHeader(File& f) {
    _numCmds = 0;

    // Program parameter records (0xD1) — skip most, capture endBehavior
    while (true) {
        uint32_t dw;
        if (!readDW(f, dw)) return 2;
        uint8_t label, type; uint16_t size;
        splitDW(dw, label, type, size);
        if (label != PIX_LABEL_PROG_PARAM) {
            f.seek(f.position() - 4);   // put back: first command DW
            break;
        }
        if (type == PP_END_BEHAVIOR && size >= 1) {
            uint32_t val;
            if (!readDW(f, val)) return 2;
            _endBehavior = (PixEndBehavior)(val & 0xFF);
            for (int i = 1; i < (int)size; i++) { uint32_t v; readDW(f, v); }
        } else {
            for (int i = 0; i < (int)size; i++) { uint32_t v; if (!readDW(f, v)) return 2; }
        }
    }

    // Command records (0xA1)
    while (_numCmds < MAX_CMDS) {
        uint32_t dw;
        if (!readDW(f, dw)) break;
        uint8_t label, type; uint16_t size;
        splitDW(dw, label, type, size);
        if (label != PIX_LABEL_COMMAND) break;

        if (type != PIX_CMD_PICTURE) {
            // Skip unknown command body
            for (int i = 1; i < (int)size; i++) { uint32_t v; if (!readDW(f, v)) return 2; }
            continue;
        }

        Command& cmd = _cmds[_numCmds];
        memset(&cmd, 0, sizeof(cmd));
        cmd.frequency = 100; // default if not specified

        int remaining = (int)size - 1; // header DW already consumed
        while (remaining > 0) {
            uint32_t pdw;
            if (!readDW(f, pdw)) return 2;
            remaining--;
            uint8_t pl, pt; uint16_t ps;
            splitDW(pdw, pl, pt, ps);
            if (pl != PIX_LABEL_CMD_PARAM) { f.seek(f.position() - 4); break; }

            uint32_t val = 0;
            if (ps >= 1) { if (!readDW(f, val)) return 2; remaining--; }
            for (int i = 1; i < (int)ps; i++) { uint32_t v; if (!readDW(f, v)) return 2; remaining--; }

            switch (pt) {
                case CP_START_TIME: cmd.startTime = ((int32_t)val < 0) ? 0 : val; break;
                case CP_END_TIME:   cmd.endTime   = val; break;
                case CP_OFFSET:     cmd.offset    = val; break;
                case CP_WIDTH:      cmd.width     = val; break;
                case CP_HEIGHT:     cmd.height    = val; break;
                case CP_FREQUENCY:  cmd.frequency = val; break;
                case CP_LAST_CMD:   cmd.isLast    = (val == 1); break;
                default: break;
            }
        }

        _numCmds++;
        cmd.frequency = clampFrequencyForDriver(_leds, cmd.frequency);
        if (cmd.isLast) break;
    }

    return _numCmds > 0 ? 0 : 2;
}

int PixPlayer::loadAxp(File& f) {
    uint32_t version = 0;
    uint32_t commandCount = 0;
    uint32_t numLeds = 0;
    uint32_t endBehavior = 0;
    uint32_t decodedBytes = 0;

    if (!readDW(f, version) || !readDW(f, commandCount) || !readDW(f, numLeds) ||
        !readDW(f, endBehavior) || !readDW(f, decodedBytes)) {
        return 2;
    }
    if ((version < AXP_VERSION || version > 2) || commandCount == 0 || commandCount > MAX_CMDS || decodedBytes == 0) return 2;

    _numCmds = (int)commandCount;
    _endBehavior = (PixEndBehavior)(endBehavior & 0xFF);
    if (_endBehavior > PixEndBehavior::PingPong) _endBehavior = PixEndBehavior::Repeat;
    uint16_t expectedWidth = _leds.logicalNumLeds();
    if (numLeds != 0 && numLeds != expectedWidth) return PIX_ERR_LED_COUNT_MISMATCH;
    size_t maxCommandBytes = 0;

    for (int i = 0; i < _numCmds; i++) {
        Command& cmd = _cmds[i];
        memset(&cmd, 0, sizeof(cmd));

        uint32_t dataOffset = 0;
        uint32_t dataSize = 0;
        uint32_t codec = 0;
        uint32_t isLast = 0;
        if (!readDW(f, cmd.startTime) || !readDW(f, cmd.endTime) || !readDW(f, cmd.width) ||
            !readDW(f, cmd.height) || !readDW(f, cmd.frequency) || !readDW(f, dataOffset) ||
            !readDW(f, dataSize) || !readDW(f, cmd.offset) || !readDW(f, codec) ||
            !readDW(f, isLast)) {
            return 2;
        }
        cmd.isLast = (isLast != 0);
        _cmdBufOffset[i] = cmd.offset;
        _axpDataOffset[i] = dataOffset;
        _axpDataSize[i] = dataSize;
        _axpCodec[i] = codec;

        if (cmd.width == 0 || cmd.height == 0 || cmd.frequency == 0) return 2;
        cmd.frequency = clampFrequencyForDriver(_leds, cmd.frequency);
        if (cmd.width != expectedWidth) return PIX_ERR_LED_COUNT_MISMATCH;
        size_t rawBytes = (size_t)cmd.width * cmd.height * 4;
        if (rawBytes > maxCommandBytes) maxCommandBytes = rawBytes;
        if ((uint64_t)cmd.offset + (uint64_t)rawBytes > decodedBytes) return 2;
        if (codec != AXP_CODEC_RAW && codec != AXP_CODEC_COLUMNS && codec != AXP_CODEC_LZSS) return 2;
        if (codec == AXP_CODEC_LZSS && version < 2) return 2;
        (void)numLeds;
    }

    _preloadBuf = allocDecodedProgramBuffer(decodedBytes);
    if (!_preloadBuf) {
        if (!_axpCmdCache[0] || _axpCmdCacheSize < maxCommandBytes) {
            for (int i = 0; i < 2; i++) {
                heap_caps_free(_axpCmdCache[i]);
                _axpCmdCache[i] = nullptr;
                _axpCachedCmd[i] = -1;
            }
            _axpCmdCacheSize = 0;
            _axpCmdCache[0] = allocAxpCommandCache(maxCommandBytes);
            if (!_axpCmdCache[0]) return 6;
            _axpCmdCache[1] = allocAxpCommandCache(maxCommandBytes);
            _axpCmdCacheSize = maxCommandBytes;
        }
        _cacheMutex = xSemaphoreCreateMutex();
        _prefetchQueue = xQueueCreate(1, sizeof(int));
        if (!_cacheMutex || !_prefetchQueue) return 6;
        _format = ProgramFormat::Axp;
        _preloaded = false;
        _axpStreaming = true;
        _axpConsumerCmd = 0;
        if (!decodeAxpCommand(f, 0, _axpCmdCache[0])) return 2;
        _axpCachedCmd[0] = 0;
        LOG("[axp] core-0 command cache: %u bytes x %u, %d commands\n",
            (unsigned)maxCommandBytes, _axpCmdCache[1] ? 2u : 1u, _numCmds);
        return 0;
    }
    memset(_preloadBuf, 0, decodedBytes);

    for (int i = 0; i < _numCmds; i++) {
        const Command& cmd = _cmds[i];
        uint8_t* dst = _preloadBuf + _cmdBufOffset[i];
        (void)cmd;
        if (!decodeAxpCommand(f, i, dst)) return 2;
    }

    _format = ProgramFormat::Axp;
    _preloaded = true;
    _axpStreaming = false;
    LOG("[axp] decoded %u bytes into PSRAM, %d commands\n", (unsigned)decodedBytes, _numCmds);
    return 0;
}

bool PixPlayer::decodeAxpCommand(File& f, int cmdIdx, uint8_t* dst) {
    if (cmdIdx < 0 || cmdIdx >= _numCmds || !dst) return false;
    const Command& cmd = _cmds[cmdIdx];
    size_t rawBytes = (size_t)cmd.width * cmd.height * 4;
    uint32_t dataOffset = _axpDataOffset[cmdIdx];
    uint32_t dataSize = _axpDataSize[cmdIdx];
    uint32_t codec = _axpCodec[cmdIdx];

    if (!f.seek(dataOffset)) return false;

    if (codec == AXP_CODEC_RAW) {
        if (dataSize != rawBytes) return false;
        return f.read(dst, rawBytes) == (int)rawBytes;
    }

    if (codec == AXP_CODEC_LZSS) {
        if (dataSize == 0) return false;
        bool ok = false;
        uint8_t* encoded = (uint8_t*)heap_caps_malloc(dataSize, MALLOC_CAP_SPIRAM);
        if (!encoded && dataSize <= 96 * 1024) {
            encoded = (uint8_t*)heap_caps_malloc(dataSize, MALLOC_CAP_INTERNAL);
        }
        if (encoded) {
            ok = (f.read(encoded, dataSize) == (int)dataSize) &&
                 decodeLzssBuffer(encoded, dataSize, dst, rawBytes);
            heap_caps_free(encoded);
        } else {
            ok = decodeLzss(f, dataSize, dst, rawBytes);
        }
        return ok;
    }

    if (codec != AXP_CODEC_COLUMNS) return false;

    for (uint32_t col = 0; col < cmd.height; col++) {
        uint8_t* colDst = dst + (size_t)col * cmd.width * 4;
        uint8_t method = 0;
        if (!readByte(f, method)) return false;

        if (method == AXP_COLUMN_RAW) {
            size_t colBytes = (size_t)cmd.width * 4;
            if (f.read(colDst, colBytes) != (int)colBytes) return false;
        } else if (method == AXP_COLUMN_REPEAT) {
            if (col == 0) return false;
            memcpy(colDst, colDst - (size_t)cmd.width * 4, (size_t)cmd.width * 4);
        } else if (method == AXP_COLUMN_RLE) {
            uint16_t runs = 0;
            if (!readU16(f, runs)) return false;
            uint32_t written = 0;
            for (uint16_t r = 0; r < runs; r++) {
                uint16_t count = 0;
                uint32_t pixel = 0;
                if (!readU16(f, count) || !readDW(f, pixel)) return false;
                if (count == 0 || written + count > cmd.width) return false;
                for (uint16_t n = 0; n < count; n++) {
                    memcpy(colDst + (size_t)(written + n) * 4, &pixel, 4);
                }
                written += count;
            }
            if (written != cmd.width) return false;
        } else {
            return false;
        }
    }

    return true;
}

// ── load ──────────────────────────────────────────────────────────────────────

int PixPlayer::load(const char* path) {
    unload();
    strncpy(_path, path, sizeof(_path) - 1);

    if (!_storage.exists(path)) return 5;

    // Detect AuraX compressed format first. Legacy .pix starts with 0xD1 records.
    {
        File f = _storage.open(path, "r");
        if (!f) return 4;
        uint32_t magic = 0;
        if (!readDW(f, magic)) {
            f.close();
            return 2;
        }
        int err = 0;
        if (magic == AXP_MAGIC) {
            err = loadAxp(f);
        } else {
            f.seek(0);
            err = parseHeader(f);
        }
        f.close();
        if (err) return err;
    }

    if (_format == ProgramFormat::Axp) {
        _loaded         = true;
        _keepFrozen     = false;
        _inPause        = false;
        _curCmd         = 0;
        _curCol         = 0;
        _framesRendered = 0;
        _framesExpected = 0;
        _programStartUs = esp_timer_get_time();
        _fpsWindowFrames = 0;
        _fpsWindowStartUs = _programStartUs;
        _currentFpsX10 = 0;
        _nextFrameUs    = _programStartUs;
        return 0;
    }

    // Validate
    for (int i = 0; i < _numCmds; i++) {
        if (_cmds[i].width == 0 || _cmds[i].height == 0 || _cmds[i].frequency == 0) return 2;
        if (_cmds[i].width != _leds.logicalNumLeds()) return PIX_ERR_LED_COUNT_MISMATCH;
        LOG("[pix] cmd %d: %dx%d @ %d Hz, t=%u-%u ms\n",
            i, _cmds[i].width, _cmds[i].height, _cmds[i].frequency,
            _cmds[i].startTime, _cmds[i].endTime);
    }

    // Calculate total image data size across all commands
    size_t totalBytes = 0;
    for (int i = 0; i < _numCmds; i++)
        totalBytes += (size_t)_cmds[i].width * _cmds[i].height * 4;

    // Try to preload into PSRAM
    _preloadBuf = (uint8_t*)heap_caps_malloc(totalBytes, MALLOC_CAP_SPIRAM);
    if (_preloadBuf) {
        File f = _storage.open(path, "r");
        if (!f) { heap_caps_free(_preloadBuf); _preloadBuf = nullptr; goto streaming; }

        size_t bufPos = 0;
        for (int i = 0; i < _numCmds; i++) {
            Command& cmd = _cmds[i];
            _cmdBufOffset[i] = bufPos;
            size_t imgBytes = (size_t)cmd.width * cmd.height * 4;
            if (!f.seek(cmd.offset) || f.read(_preloadBuf + bufPos, imgBytes) != (int)imgBytes) {
                f.close();
                heap_caps_free(_preloadBuf); _preloadBuf = nullptr;
                goto streaming;
            }
            bufPos += imgBytes;
        }
        f.close();
        _preloaded = true;
        LOG("[pix] preloaded %zu bytes into PSRAM, %d commands\n", totalBytes, _numCmds);
    } else {
        streaming:
        // Streaming: allocate single-column scratch buffer in internal DRAM
        uint32_t maxWidth = 0;
        for (int i = 0; i < _numCmds; i++)
            if (_cmds[i].width > maxWidth) maxWidth = _cmds[i].width;

        _colBuf = (uint8_t*)malloc(maxWidth * 4);
        if (!_colBuf) return 6;

        _file = _storage.open(path, "r");
        if (!_file) { free(_colBuf); _colBuf = nullptr; return 4; }
        LOG("[pix] streaming mode, %d commands\n", _numCmds);
    }

    _loaded         = true;
    _keepFrozen     = false;
    _inPause        = false;
    _curCmd         = 0;
    _curCol         = 0;
    _framesRendered = 0;
    _framesExpected = 0;
    _programStartUs = esp_timer_get_time();
    _fpsWindowFrames = 0;
    _fpsWindowStartUs = _programStartUs;
    _currentFpsX10 = 0;
    _nextFrameUs    = _programStartUs;
    return 0;
}

void PixPlayer::setBrightness(uint8_t pct) {
    _leds.setBrightness(pct);
}

void PixPlayer::setTempo(uint16_t pct) {
    _tempo = pct < 1 ? 1 : (pct > 1000 ? 1000 : pct);
}

void PixPlayer::nudge(int32_t ms) {
    _programStartUs -= (int64_t)ms * 1000;
}

void PixPlayer::setEndBehavior(uint8_t v) {
    _endBehaviorOverride = v;
}

void PixPlayer::blackout() {
    stopTask();
    unload();
    _leds.clear();
}

void PixPlayer::scheduleStart(int64_t atUs) {
    _framesRendered = 0;
    _framesExpected = 0;
    _fpsWindowFrames = 0;
    _fpsWindowStartUs = atUs;
    _currentFpsX10 = 0;
    _inPause        = false;
    _programStartUs = atUs;
    _nextFrameUs    = atUs;
}

// ── playback ──────────────────────────────────────────────────────────────────

const uint8_t* PixPlayer::fetchColumn(int cmdIdx, int col) {
    const Command& cmd = _cmds[cmdIdx];
    if (_preloaded) {
        return _preloadBuf + _cmdBufOffset[cmdIdx] + (size_t)col * cmd.width * 4;
    }
    if (_format == ProgramFormat::Axp && _axpStreaming) {
        if (!_axpCmdCache[0] || !_cacheMutex || !_prefetchQueue) return nullptr;
        size_t rawBytes = (size_t)cmd.width * cmd.height * 4;
        if (rawBytes > _axpCmdCacheSize) return nullptr;

        _axpConsumerCmd = cmdIdx;
        int slot = cachedSlotFor(cmdIdx);
        if (slot < 0) {
            requestPrefetch(cmdIdx);
            int64_t deadlineUs = esp_timer_get_time() + 3000000LL;
            while (_taskRunning && esp_timer_get_time() < deadlineUs) {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
                slot = cachedSlotFor(cmdIdx);
                if (slot >= 0) break;
                requestPrefetch(cmdIdx);
            }
            if (slot < 0) {
                LOG("[axp] command prefetch timeout: %d\n", cmdIdx);
                return nullptr;
            }
        }
        requestPrefetch((cmdIdx + 1) % _numCmds);
        return _axpCmdCache[slot] + (size_t)col * cmd.width * 4;
    }
    // Streaming
    uint32_t byteOffset = cmd.offset + (uint32_t)col * cmd.width * 4;
    if (!_file.seek(byteOffset)) return nullptr;
    if (_file.read(_colBuf, cmd.width * 4) != (int)(cmd.width * 4)) return nullptr;
    return _colBuf;
}

bool PixPlayer::update() {
    if (!_loaded) return false;

    int64_t now = esp_timer_get_time();
    if (now < _nextFrameUs) return true;

    // Scale elapsed real time by tempo to get effective program time.
    // tempo=100 → normal; tempo=200 → 2× speed (program advances 2× faster).
    int64_t programUs = (now - _programStartUs) * (int64_t)_tempo / 100LL;

    if (_keepFrozen) {
        // Keep behavior: last frame already on LEDs, just park the task
        _nextFrameUs = now + 100000LL;  // wake up every 100 ms to stay alive
        return true;
    }

    PixEndBehavior effectiveBehavior = (_endBehaviorOverride != 255)
        ? (PixEndBehavior)_endBehaviorOverride
        : _endBehavior;

    // Advance past any commands whose endTime has passed
    while (programUs >= (int64_t)_cmds[_curCmd].endTime * 1000) {
        _curCmd++;
        _curCol = 0;
        _pingPongReverse = false;
        if (_curCmd >= _numCmds) {
            if (effectiveBehavior == PixEndBehavior::Exit) {
                _loaded = false;
                _leds.clear();
                return false;
            } else if (effectiveBehavior == PixEndBehavior::Keep) {
                _curCmd     = _numCmds - 1;
                _curCol     = _cmds[_curCmd].height - 1;
                _keepFrozen = true;
                break;
            } else {
                _curCmd = 0;
                _programStartUs = now;
                programUs = 0;
            }
        }
    }

    // Convert program startTime back to real time accounting for tempo
    int64_t cmdStartUs = _programStartUs + (int64_t)_cmds[_curCmd].startTime * 1000LL * 100LL / (int64_t)_tempo;
    if (now < cmdStartUs) {
        if (!_inPause) {
            _leds.clear();
            _inPause = true;
        }
        _nextFrameUs = cmdStartUs;
        return true;
    }
    _inPause = false;

    const Command& cmd = _cmds[_curCmd];
    int64_t frameIntervalUs = 1000000LL * 100LL / ((int64_t)cmd.frequency * (int64_t)_tempo);
    if (frameIntervalUs < 1) frameIntervalUs = 1;
    _framesExpected += (now - _nextFrameUs) / frameIntervalUs + 1;
    _framesRendered++;
    _fpsWindowFrames++;
    int64_t fpsElapsedUs = now - _fpsWindowStartUs;
    if (fpsElapsedUs >= 1000000LL) {
        _currentFpsX10 = (uint16_t)((_fpsWindowFrames * 10000000ULL + (uint64_t)fpsElapsedUs / 2) / (uint64_t)fpsElapsedUs);
        _fpsWindowFrames = 0;
        _fpsWindowStartUs = now;
    }

    int64_t cmdElapsedUs = programUs - (int64_t)cmd.startTime * 1000LL;
    if (cmdElapsedUs < 0) cmdElapsedUs = 0;
    uint32_t frameIndex = (uint32_t)((uint64_t)cmdElapsedUs * (uint64_t)cmd.frequency / 1000000ULL);
    if (effectiveBehavior == PixEndBehavior::PingPong && cmd.height > 1) {
        uint32_t seqLen = cmd.height * 2u - 2u;
        uint32_t pos = frameIndex % seqLen;
        _curCol = (pos < cmd.height) ? (int)pos : (int)(seqLen - pos);
    } else {
        _curCol = (int)(frameIndex % cmd.height);
    }

    const uint8_t* col = fetchColumn(_curCmd, _curCol);
    if (col) {
        if (_curCmd == 0 && _curCol == 0 && programUs < 2000000LL / (int64_t)cmd.frequency) {
            uint8_t maxBri = 0, maxR = 0, maxG = 0, maxB = 0;
            for (int i = 0; i < (int)cmd.width; i++) {
                if ((col[i*4] & 0x1F) > maxBri) maxBri = col[i*4] & 0x1F;
                if (col[i*4+1] > maxB) maxB = col[i*4+1];
                if (col[i*4+2] > maxG) maxG = col[i*4+2];
                if (col[i*4+3] > maxR) maxR = col[i*4+3];
            }
            LOG("[pix] col0 max: bri=%d B=%d G=%d R=%d\n", maxBri, maxB, maxG, maxR);
        }
        _leds.showColumnDirect(col, cmd.width);
    }

    int64_t nextProgramUs = (int64_t)cmd.startTime * 1000LL
                          + (int64_t)(frameIndex + 1u) * 1000000LL / (int64_t)cmd.frequency;
    _nextFrameUs = _programStartUs + nextProgramUs * 100LL / (int64_t)_tempo;
    while (_nextFrameUs <= now) _nextFrameUs += frameIntervalUs;
    return true;
}

// ── FreeRTOS task ─────────────────────────────────────────────────────────────

void PixPlayer::taskEntry(void* arg) {
    auto* p = static_cast<PixPlayer*>(arg);
    p->runTask();
    p->_taskHandle = nullptr;  // signal completion before self-delete
    vTaskDelete(nullptr);
}

int PixPlayer::cachedSlotFor(int cmdIdx) {
    if (!_cacheMutex) return -1;
    int slot = -1;
    xSemaphoreTake(_cacheMutex, portMAX_DELAY);
    for (int i = 0; i < 2; i++) {
        if (_axpCmdCache[i] && !_axpCacheLoading[i] && _axpCachedCmd[i] == cmdIdx) {
            slot = i;
            break;
        }
    }
    xSemaphoreGive(_cacheMutex);
    return slot;
}

void PixPlayer::requestPrefetch(int cmdIdx) {
    if (!_prefetchQueue || !_cacheMutex || cmdIdx < 0 || cmdIdx >= _numCmds) return;
    bool needed = true;
    xSemaphoreTake(_cacheMutex, portMAX_DELAY);
    for (int i = 0; i < 2; i++) {
        if (_axpCmdCache[i] && (_axpCachedCmd[i] == cmdIdx ||
            (_axpCacheLoading[i] && _axpCachedCmd[i] == cmdIdx))) {
            needed = false;
            break;
        }
    }
    xSemaphoreGive(_cacheMutex);
    if (needed) xQueueOverwrite(_prefetchQueue, &cmdIdx);
}

void PixPlayer::prefetchTaskEntry(void* arg) {
    auto* p = static_cast<PixPlayer*>(arg);
    p->runPrefetchTask();
    p->_prefetchTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

void PixPlayer::runPrefetchTask() {
    File file = _storage.open(_path, "r");
    if (!file) {
        LOGLN("[storage] prefetch file open failed");
        return;
    }

    while (!_prefetchStop) {
        int cmdIdx = -1;
        if (xQueueReceive(_prefetchQueue, &cmdIdx, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        if (_prefetchStop || cmdIdx < 0 || cmdIdx >= _numCmds) continue;
        if (cachedSlotFor(cmdIdx) >= 0) continue;

        int slot = -1;
        xSemaphoreTake(_cacheMutex, portMAX_DELAY);
        for (int i = 0; i < 2; i++) {
            if (_axpCmdCache[i] && !_axpCacheLoading[i] && _axpCachedCmd[i] != _axpConsumerCmd) {
                slot = i;
                break;
            }
        }
        if (slot >= 0) {
            _axpCachedCmd[slot] = cmdIdx;
            _axpCacheLoading[slot] = true;
        }
        xSemaphoreGive(_cacheMutex);

        if (slot < 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
            requestPrefetch(cmdIdx);
            continue;
        }

        bool ok = decodeAxpCommand(file, cmdIdx, _axpCmdCache[slot]);
        xSemaphoreTake(_cacheMutex, portMAX_DELAY);
        _axpCacheLoading[slot] = false;
        if (!ok) _axpCachedCmd[slot] = -1;
        xSemaphoreGive(_cacheMutex);
        if (!ok) LOG("[storage] command prefetch failed: %d\n", cmdIdx);
        if (_taskHandle) xTaskNotifyGive(_taskHandle);
    }
    file.close();
}

bool PixPlayer::startPrefetchTask() {
    if (!_axpStreaming) return true;
    if (_prefetchTaskHandle) return true;
    _prefetchStop = false;
    BaseType_t result = xTaskCreatePinnedToCore(
        prefetchTaskEntry, "program_prefetch", 8192, this,
        AURAX_STORAGE_TASK_PRIORITY, &_prefetchTaskHandle, 0);
    if (result != pdPASS) {
        _prefetchTaskHandle = nullptr;
        LOGLN("[storage] failed to start core-0 prefetch task");
        return false;
    }
    if (_numCmds > 1) requestPrefetch(1);
    return true;
}

void PixPlayer::stopPrefetchTask() {
    if (!_prefetchTaskHandle) return;
    _prefetchStop = true;
    if (_prefetchQueue) {
        int stop = -1;
        xQueueOverwrite(_prefetchQueue, &stop);
    }
    while (_prefetchTaskHandle) vTaskDelay(1);
}

void PixPlayer::runTask() {
    while (_taskRunning) {
        if (!update()) break;
        int64_t remaining = _nextFrameUs - esp_timer_get_time();
        if (remaining > 10000) {
            // > 10 ms: sleep, uvolni CPU ostatním taskům
            vTaskDelay(pdMS_TO_TICKS(remaining / 1000 - 5));  // -5 ms jako margin
        } else {
            // < 10 ms: spinovat s přesností esp_timer, ne vTaskDelay
            while (_taskRunning && esp_timer_get_time() < _nextFrameUs) {
#if defined(ARDUINO_ARCH_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_FREERTOS_UNICORE)
                int64_t c3Remaining = _nextFrameUs - esp_timer_get_time();
                if (c3Remaining > 1500) vTaskDelay(1);
                else taskYIELD();
#else
                taskYIELD();
#endif
            }
        }
    }
    _taskRunning = false;
}

void PixPlayer::startTask(uint8_t core, uint32_t stackSize) {
    if (_taskHandle) return;
    if (!startPrefetchTask()) return;
    _taskRunning = true;
    xTaskCreatePinnedToCore(taskEntry, "pix_player", stackSize, this, AURAX_PIX_TASK_PRIORITY, &_taskHandle, core);
}

void PixPlayer::stopTask() {
    if (_taskHandle) {
        _taskRunning = false;
        while (_taskHandle) vTaskDelay(1);  // wait for task to signal completion
    }
    stopPrefetchTask();
}
