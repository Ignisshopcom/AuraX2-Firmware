#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include "led_driver.h"

// progEndBehavior values
enum class PixEndBehavior : uint8_t {
    Exit   = 0,
    Repeat = 1,
    Keep   = 2,
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
    void startTask(uint8_t core = 1, uint32_t stackSize = 4096);
    void stopTask();

    // After load(), delay actual playback start to an absolute esp_timer time.
    void scheduleStart(int64_t atUs);
    void blackout();   // stop playback and turn off all LEDs

    bool isLoaded()     const { return _loaded; }
    int  numCommands()  const { return _numCmds; }

private:
    static constexpr int MAX_CMDS = 64;

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

    // Preloaded mode: entire file image section in PSRAM
    uint8_t* _preloadBuf = nullptr;           // raw bytes for all images
    size_t   _cmdBufOffset[MAX_CMDS] = {};    // byte offset of each command's data inside _preloadBuf
    bool     _preloaded  = false;

    // Streaming mode
    char     _path[128]  = {};
    File     _file;
    uint8_t* _colBuf     = nullptr;           // single-column scratch buffer (internal DRAM)

    // Playback state
    int     _curCmd         = 0;
    int     _curCol         = 0;
    int64_t _nextFrameUs    = 0;
    int64_t _programStartUs = 0;  // esp_timer time when program playback began

    TaskHandle_t     _taskHandle  = nullptr;
    volatile bool    _taskRunning = false;
};
