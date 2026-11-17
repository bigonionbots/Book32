#pragma once
#include "Config.h"
#if defined(BOARD_M5_PAPERDIY)
#include <Adafruit_GFX.h>
#include <FastEPD.h>
#include "LilygoLayout.h"

// M5Stack PaperDIY: the M5PaperS3's 960x540 parallel panel through FastEPD
// (BB_PANEL_M5PAPERS3), powered by the M5PM1's rail. A GFX canvas in the
// LILYGO's safe area (same panel and bezel margins, LilygoLayout), rotated
// to portrait by the UI, with the GxEPD2-style calls the Book32 UI makes.
class PaperDiyDisplay : public GFXcanvas1 {
public:
    PaperDiyDisplay() : GFXcanvas1(LilygoLayout::CANVAS_W, LilygoLayout::CANVAS_H) {}
    void init(uint32_t = 115200, bool = true, uint16_t = 10, bool = false);
    void setFullWindow() { _partial = false; }
    void setPartialWindow(int16_t, int16_t, int16_t, int16_t) { _partial = true; }
    void firstPage() {}
    bool nextPage() { refresh(_partial); return false; }
    void refresh(bool partial = false);
    void clearScreen(uint8_t value = 0xff);
    // The panel's high voltage off between refreshes (FastEPD turns it on as needed).
    void powerOff();
    void hibernate();
    // Grayscale sleep cover, as LilygoDisplay::drawGrayCover.
    bool drawGrayCover(uint8_t* gray, int x, int y, int w, int h);
    bool ready() const { return _ready; }
private:
    FASTEPD _panel;
    bool _ready = false, _partial = false;
    bool _forceFull = false;  // the panel buffers hold a 4-bit image
    void copyCanvas(const uint8_t* canvas);
    void checkResult(int result);
};
#endif
