#include "PaperDiyDisplay.h"
#if defined(BOARD_M5_PAPERDIY)
#include "PaperDiyPower.h"
#include "StickyGrayImage.h"

namespace {
// The bus clock. FastEPD's PaperS3 default is 40 MHz and M5GFX runs the
// PaperDIY at 16 MHz; 20 MHz is the LILYGO's, for the same panel.
constexpr uint32_t BUS_HZ = 20000000;
constexpr size_t PLANE_BYTES = LilygoLayout::PANEL_W * LilygoLayout::PANEL_H / 8;

// Gray waveform for the sleep cover: the LILYGO's tables (same ED047TC1
// panel). FastEPD keeps a pointer to the matrix: levels x passes, 1 darkens,
// 2 lightens.
constexpr int MAX_GRAY_PASSES = 32;
uint8_t grayMatrix[16 * MAX_GRAY_PASSES];
int grayPasses = 0;

void buildGrayMatrix() {
    const uint8_t* blacks = StickyGrayImage::LILYGO_BLACKS;
    const uint8_t* whites = StickyGrayImage::LILYGO_WHITES;
    int passes = 1;
    for (int level = 0; level < 16; ++level) passes = max(passes, blacks[level] + whites[level]);
    passes = min(passes, MAX_GRAY_PASSES);
    for (int level = 0; level < 16; ++level) {
        const int start = passes - blacks[level] - whites[level];
        for (int pass = 0; pass < passes; ++pass)
            grayMatrix[level * passes + pass] = pass < start ? 0 : pass < passes - whites[level] ? 1 : 2;
    }
    grayPasses = passes;
}
}  // namespace

void PaperDiyDisplay::init(uint32_t, bool, uint16_t, bool) {
    // The rail first: FastEPD then drives BST_EN (GPIO46) as the panel's PWR pin.
    if (!PaperDiyPower::begin()) {
        Serial.println("PaperDIY: no M5PM1 answered; display disabled");
        return;
    }
    PaperDiyPower::setEpdPower(true);
    delay(10);
    _ready = _panel.initPanel(BB_PANEL_M5PAPERS3, BUS_HZ) == BBEP_SUCCESS;
    if (_ready) _ready = _panel.setMode(BB_MODE_1BPP) == BBEP_SUCCESS;
    buildGrayMatrix();
    Serial.printf("PaperDIY: panel %s\n", _ready ? "ready" : "failed");
    if (_ready) clearScreen();
}

void PaperDiyDisplay::copyCanvas(const uint8_t* canvas) {
    using namespace LilygoLayout;
    // Both buffers are MSB-first, native landscape, white = 1.
    memset(_panel.currentBuffer(), 0xff, PLANE_BYTES);
    for (int y = 0; y < CANVAS_H; ++y)
        memcpy(_panel.currentBuffer() + (y + INSET) * (PANEL_W / 8) + INSET / 8, canvas + y * PITCH, PITCH);
}

void PaperDiyDisplay::refresh(bool partial) {
    if (!_ready || !getBuffer() || !_panel.currentBuffer()) return;
    copyCanvas(getBuffer());
    if (_forceFull && partial) {
        // The reference is a 4-bit image (the sleep cover). The inverse of
        // the new frame as reference drives every pixel without a flash.
        _panel.backupPlane();
        uint8_t* reference = _panel.previousBuffer();
        for (size_t i = 0; i < PLANE_BYTES; ++i) reference[i] = ~reference[i];
    }
    _forceFull = false;
    checkResult(partial ? _panel.partialUpdate(false) : _panel.fullUpdate(CLEAR_SLOW, false));
}

void PaperDiyDisplay::clearScreen(uint8_t value) {
    if (getBuffer()) memset(getBuffer(), value, LilygoLayout::PITCH * LilygoLayout::CANVAS_H);
    refresh(false);
}

void PaperDiyDisplay::powerOff() {
    if (_ready) _panel.einkPower(0);
}

void PaperDiyDisplay::hibernate() {
    if (_ready) _panel.einkPower(0);
    PaperDiyPower::setEpdPower(false);
}

bool PaperDiyDisplay::drawGrayCover(uint8_t* gray, int x, int y, int w, int h) {
    using namespace LilygoLayout;
    if (!_ready || !getBuffer() || !_panel.currentBuffer() || !gray) return false;
    int16_t* errors = static_cast<int16_t*>(ps_malloc(sizeof(int16_t) * 2 * (w + 2)));
    if (!errors) return false;
    StickyGrayImage::quantize(gray, w, h, errors, StickyGrayImage::LILYGO_TONES, StickyGrayImage::LILYGO_TONE_COUNT);
    free(errors);
    if (_panel.setMode(BB_MODE_4BPP) != BBEP_SUCCESS) return false;
    if (grayPasses && _panel.setCustomMatrix(grayMatrix, 16 * grayPasses) != BBEP_SUCCESS) {
        _panel.setMode(BB_MODE_1BPP);  // the caller falls back to a dithered cover
        return false;
    }
    // 4-bit native-landscape buffer, two pixels a byte (high nibble first),
    // 15 white: the canvas in black and white, then the cover in gray.
    uint8_t* panel = _panel.currentBuffer();
    constexpr int NATIVE_PITCH = PANEL_W / 2;
    memset(panel, 0xff, NATIVE_PITCH * PANEL_H);
    auto put = [&](int nx, int ny, uint8_t level) {
        uint8_t& pair = panel[ny * NATIVE_PITCH + nx / 2];
        pair = (nx & 1) ? (pair & 0xf0) | level : (pair & 0x0f) | (level << 4);
    };
    const uint8_t* canvas = getBuffer();
    for (int ry = 0; ry < CANVAS_H; ++ry)
        for (int rx = 0; rx < CANVAS_W; ++rx)
            if (!(canvas[ry * PITCH + rx / 8] & (0x80 >> (rx & 7)))) put(rx + INSET, ry + INSET, 0);
    const uint8_t rotation = getRotation();
    for (int row = 0; row < h; ++row) {
        const int py = y + row;
        if (py < 0 || py >= height()) continue;
        for (int col = 0; col < w; ++col) {
            const int px = x + col;
            if (px < 0 || px >= width()) continue;
            const int rx = rotation == 1 ? CANVAS_W - 1 - py : py;
            const int ry = rotation == 1 ? px : CANVAS_H - 1 - px;
            put(rx + INSET, ry + INSET, gray[row * w + col]);
        }
    }
    checkResult(_panel.fullUpdate(CLEAR_SLOW, false));
    _panel.setMode(BB_MODE_1BPP);
    _forceFull = true;  // the previous-image buffer shares memory with the 4-bit frame
    return _ready;
}

void PaperDiyDisplay::checkResult(int result) {
    if (result != BBEP_SUCCESS) {
        Serial.printf("PaperDIY: display error %d; disabling panel\n", result);
        _panel.einkPower(0);
        _ready = false;
    }
}

#endif
