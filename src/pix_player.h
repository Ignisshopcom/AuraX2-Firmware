#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include "led_driver.h"
#include "task_compat.h"

// progEndBehavior values
enum class PixEndBehavior : uint8_t {
    Exit     = 0,
    Repeat   = 1,
    Keep     = 2,
    PingPong = 3,   // columns reverse direction at each end (mirror effect)
};

class PixPlayer {
public:
    explicit PixPlayer(ILedDriver& leds);
    ~PixPlayer();

    // Mount LittleFS (if not already mounted) and load the file.
    // Tries to preload all images into PSRAM; falls back to streaming.
    // Returns 0 on success, non-zero error code on failure.
    int load(const char* path);
    void unload();

    // Call from loop() at full speed — shows the next column if it is time.
    // Returns false when playback has finished (endBehavior == Exit).
    bool update();

    // Run playback in a dedicated FreeRTOS task (blocking loop inside).
    void startTask(uint8_t core = AURAX_LED_TASK_CORE, uint32_t stackSize = 4096);
    void stopTask();

    // After load(), delay actual playback start to an absolute esp_timer time.
    void scheduleStart(int64_t atUs);
    void blackout();   // stop playback and turn off all LEDs
    void setBrightness(uint8_t pct);   // delegates to ILedDriver::setBrightness()
    void setTempo(uint16_t pct);       // 100 = normal, 50 = half speed, 200 = double speed
    void nudge(int32_t ms);            // shift program timeline (+ = forward, - = backward)
    // Override end-of-show behavior. 255 = use value from .pix file (default).
    // 0 = Exit (blackout), 1 = Repeat (loop), 2 = Keep last frame, 3 = PingPong.
    void setEndBehavior(uint8_t v);

    bool isLoaded()     const { return _loaded; }
    int  numCommands()  const { return _numCmds; }

    struct Stats {
        uint32_t framesRendered;  // snímků skutečně vykreslených
        uint32_t framesExpected;  // snímků, které měly být vykresleny podle časování
        uint16_t fpsX10;          // current rendered FPS * 10
    };
    Stats stats() const { return {_framesRendered, _framesExpected, _currentFpsX10}; }

private:
    static constexpr int MAX_CMDS = 64;

    enum class ProgramFormat : uint8_t {
        Pix = 0,
        Axp = 1,
    };

    struct Command {
        uint32_t startTime;   // ms — when this command starts in the program timeline
        uint32_t endTime;     // ms — when this command ends (loop until here)
        uint32_t offset;      // byte offset of image data in file
        uint32_t width;       // pixels per column = numLeds
        uint32_t height;      // number of columns
        uint32_t frequency;   // Hz
        bool     isLast;
    };

    int  parseHeader(File& f);
    int  loadAxp(File& f);
    bool decodeAxpCommand(File& f, int cmdIdx, uint8_t* dst);
    // Returns pointer to 4*width raw bytes [dim,B,G,R] for column col of command cmdIdx.
    // Returns nullptr on error.
    const uint8_t* fetchColumn(int cmdIdx, int col);

    static void taskEntry(void* arg);
    void        runTask();

    ILedDriver& _leds;

    Command _cmds[MAX_CMDS];
    int     _numCmds     = 0;
    PixEndBehavior _endBehavior = PixEndBehavior::Repeat;
    bool    _loaded      = false;
    ProgramFormat _format = ProgramFormat::Pix;

    // Preloaded mode: entire file image section in PSRAM
    uint8_t* _preloadBuf = nullptr;           // raw bytes for all images
    size_t   _cmdBufOffset[MAX_CMDS] = {};    // byte offset of each command's data inside _preloadBuf
    bool     _preloaded  = false;

    // AXP streaming/cache mode for ESP32 variants without PSRAM.
    uint32_t _axpDataOffset[MAX_CMDS] = {};
    uint32_t _axpDataSize[MAX_CMDS] = {};
    uint32_t _axpCodec[MAX_CMDS] = {};
    bool     _axpStreaming = false;
    uint8_t* _axpCmdCache = nullptr;           // decoded bytes for the currently active AXP command
    size_t   _axpCmdCacheSize = 0;
    int      _axpCachedCmd = -1;

    // Streaming mode
    char     _path[128]  = {};
    File     _file;
    uint8_t* _colBuf     = nullptr;           // single-column scratch buffer (internal DRAM)

    // Playback state
    int     _curCmd         = 0;
    int     _curCol         = 0;
    int64_t _nextFrameUs    = 0;
    int64_t _programStartUs = 0;  // esp_timer time when program playback began

    uint32_t         _framesRendered      = 0;
    uint32_t         _framesExpected     = 0;
    uint32_t         _fpsWindowFrames    = 0;
    int64_t          _fpsWindowStartUs   = 0;
    uint16_t         _currentFpsX10      = 0;

    uint16_t         _tempo               = 100;  // playback speed %; 100=normal, 50=half, 200=double
    uint8_t          _endBehaviorOverride = 255;  // 255=from file, 0=Exit, 1=Repeat, 2=Keep, 3=PingPong
    bool             _keepFrozen          = false; // set when Keep behavior locks last frame
    bool             _inPause             = false; // true = jsme v pauze, clear() již zavolán
    bool             _pingPongReverse     = false; // true = playing columns backward in PingPong mode

    TaskHandle_t     _taskHandle  = nullptr;
    volatile bool    _taskRunning = false;
};
