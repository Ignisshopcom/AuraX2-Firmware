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

static void splitDW(uint32_t dw, uint8_t& label, uint8_t& type, uint16_t& size) {
    label = (dw >> 24) & 0xFF;
    type  = (dw >> 16) & 0xFF;
    size  = dw & 0xFFFF;
}

// ── PixPlayer ─────────────────────────────────────────────────────────────────

PixPlayer::PixPlayer(ILedDriver& leds) : _leds(leds) {}

PixPlayer::~PixPlayer() {
    stopTask();
    unload();
}

void PixPlayer::unload() {
    if (_file) _file.close();
    heap_caps_free(_preloadBuf); _preloadBuf = nullptr;
    free(_colBuf);               _colBuf     = nullptr;
    _preloaded = false;
    _loaded    = false;
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
        if (cmd.isLast) break;
    }

    return _numCmds > 0 ? 0 : 2;
}

// ── load ──────────────────────────────────────────────────────────────────────

int PixPlayer::load(const char* path) {
    unload();
    strncpy(_path, path, sizeof(_path) - 1);

    if (!LittleFS.begin(true)) return 3;
    if (!LittleFS.exists(path)) return 5;

    // Parse header
    {
        File f = LittleFS.open(path, "r");
        if (!f) return 4;
        int err = parseHeader(f);
        f.close();
        if (err) return err;
    }

    // Validate
    for (int i = 0; i < _numCmds; i++) {
        if (_cmds[i].width == 0 || _cmds[i].height == 0 || _cmds[i].frequency == 0) return 2;
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
        File f = LittleFS.open(path, "r");
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

        _file = LittleFS.open(path, "r");
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
                taskYIELD();
            }
        }
    }
    _taskRunning = false;
}

void PixPlayer::startTask(uint8_t core, uint32_t stackSize) {
    if (_taskHandle) return;
    _taskRunning = true;
    xTaskCreatePinnedToCore(taskEntry, "pix_player", stackSize, this, 5, &_taskHandle, core);
}

void PixPlayer::stopTask() {
    if (!_taskHandle) return;
    _taskRunning = false;
    while (_taskHandle) vTaskDelay(1);  // wait for task to signal completion
}
