#include "display_ui.h"
#include "pin_config.h"
#include "config.h"
#include "settings_manager.h"
#include "tactical_suite.h"
#include <Arduino.h>
#include <math.h>
#include <SD_MMC.h>

class LGFX_ILI9341_Bus : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI       _bus_instance;

public:
    LGFX_ILI9341_Bus() {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = SPI_FREQ_HZ;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PIN_SPI_SCK;
            cfg.pin_mosi    = PIN_SPI_MOSI;
            cfg.pin_miso    = PIN_SPI_MISO;
            cfg.pin_dc      = PIN_TFT_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = PIN_TFT_CS;
            cfg.pin_rst          = PIN_TFT_RST; // -1: Tied to EN pin
            cfg.pin_busy         = -1;
            cfg.memory_width     = 240;
            cfg.memory_height    = 320;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = true;
            cfg.invert           = false;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};

static LGFX_ILI9341_Bus tft;
static LGFX_Sprite* s_canvas = nullptr;   // 240x320 PSRAM back buffer
static lgfx::LGFXBase* D = &tft;          // current draw target
static bool s_buffered = false;

void DisplayUI::init() {
    SPIScopedLock lock;
    tft.init();
    tft.setRotation(0); // 240x320 Portrait
    tft.fillScreen(THEME_BG);
    tft.setTextColor(THEME_CYAN, THEME_BG);
    tft.setTextSize(1);

    // Allocate the flicker-free PSRAM back buffer once (board guarantees 8MB OPI PSRAM)
    if (!s_canvas) {
        s_canvas = new LGFX_Sprite(&tft);
        s_canvas->setPsram(true);
        s_canvas->setColorDepth(16);
        if (!s_canvas->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT)) {
            delete s_canvas;
            s_canvas = nullptr;
            Serial.println("[UI] WARN: back buffer alloc failed, direct draw");
        }
    }
}

LGFX* DisplayUI::get() { return &tft; }
lgfx::LGFXBase* DisplayUI::canvas() { return D; }

void DisplayUI::clear(uint16_t color) {
    SPIScopedLock lock;
    tft.fillScreen(color);
}

void DisplayUI::beginFrame(uint16_t bg) {
    if (s_canvas) {
        D = s_canvas;
        s_buffered = true;
        s_canvas->fillScreen(bg);
    } else {
        D = &tft;
        s_buffered = false;
        SPISemaphore::acquire(portMAX_DELAY);
        tft.startWrite();
        tft.fillScreen(bg);
    }
}

void DisplayUI::applyBrightness(uint8_t pct) {
    if (pct >= 100) return;
    int dark = 100 - pct;              // 0..90 percent of rows to blacken
    int acc = 0;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        acc += dark;
        if (acc >= 100) {
            acc -= 100;
            D->drawFastHLine(0, y, SCREEN_WIDTH, THEME_BG);
        }
    }
}

void DisplayUI::endFrame() {
    applyBrightness(SettingsManager::getBrightness());
    if (s_buffered && s_canvas) {
        SPIScopedLock lock;
        s_canvas->pushSprite(0, 0);
    } else {
        tft.endWrite();
        SPISemaphore::release();
    }
    D = &tft;
    s_buffered = false;
}

void DisplayUI::drawIcon(int x, int y, uint8_t iconId, uint16_t fgColor, uint16_t bgColor) {
    if (iconId >= ICON_COUNT) return;
    // Icons use inverted XBM (bit 0 = foreground); swap colors to render correctly.
    D->drawXBitmap(x, y, axion_icons[iconId], ICON_W, ICON_H, bgColor, fgColor);
}

void DisplayUI::drawGraffitiCrown(int x, int y, uint16_t color) {
    // 3-point underground graffiti crown (Basquiat-inspired cyber street tag)
    // Left peak
    D->drawLine(x, y + 6, x - 4, y, color);
    D->drawLine(x - 4, y, x - 1, y + 4, color);
    // Middle peak (taller)
    D->drawLine(x - 1, y + 4, x + 2, y - 2, color);
    D->drawLine(x + 2, y - 2, x + 5, y + 4, color);
    // Right peak
    D->drawLine(x + 5, y + 4, x + 8, y, color);
    D->drawLine(x + 8, y, x + 4, y + 6, color);
    // Crown base bar
    D->drawFastHLine(x - 4, y + 7, 13, color);
}

void DisplayUI::drawGraffitiBarcode(int x, int y, uint16_t color) {
    // Cyberpunk micro barcode tag
    const uint8_t bars[] = { 1, 2, 1, 1, 2, 1, 3, 1, 2, 1 };
    int curX = x;
    for (size_t b = 0; b < sizeof(bars); b++) {
        D->fillRect(curX, y, bars[b], 6, color);
        curX += bars[b] + 1;
    }
}

void DisplayUI::drawCyberGraffitiHUD(int x, int y, const char* tag, uint16_t primaryCol, uint16_t accentCol) {
    if (!tag) tag = "AXION";

    // Dynamic Cyberpunk Hazard Slash Bar & Tag Overlays
    // 1. Tag Accent Backsplash / Stencil Slash
    D->drawLine(x - 6, y + 14, x + 2, y - 2, accentCol);
    D->drawLine(x - 5, y + 14, x + 3, y - 2, accentCol);
    D->drawLine(x - 1, y + 15, x + 7, y - 3, primaryCol);

    // 2. Neon Underglow Shadow
    D->setTextSize(1);
    D->setTextColor(THEME_PANEL, THEME_BG);
    D->setCursor(x + 11, y + 5);
    D->print(tag);

    // 3. Stencil Street Tag Text with chromatic offset
    D->setTextColor(accentCol, THEME_PANEL);
    D->setCursor(x + 9, y + 3);
    D->print(tag);

    D->setTextColor(primaryCol, THEME_PANEL);
    D->setCursor(x + 10, y + 4);
    D->print(tag);

    // 4. Cyber Drips / Sparks
    int tagLen = strlen(tag) * 6;
    int endX = x + 12 + tagLen;
    D->drawFastHLine(endX + 2, y + 6, 10, primaryCol);
    D->drawFastHLine(endX + 4, y + 9, 6, accentCol);
    D->drawPixel(endX + 14, y + 7, primaryCol);
    D->drawPixel(endX + 16, y + 10, accentCol);
    D->drawPixel(x + 16, y + 15, primaryCol); // subtle spray splatter
    D->drawPixel(x + 24, y + 16, accentCol);
}

void DisplayUI::drawAnimatedMenu(const MenuItem* items, uint8_t count, int selectedIndex, float animOffset) {
    // Header with Cyber Graffiti Tag & Crown
    D->fillRect(0, 0, SCREEN_WIDTH, 22, THEME_PANEL);
    D->drawFastHLine(0, 22, SCREEN_WIDTH, THEME_CYAN);

    // Graffiti Crown floating above tag
    drawGraffitiCrown(14, 2, THEME_AMBER);

    // Render Cyber Street Graffiti Tag at top left
    drawCyberGraffitiHUD(4, 5, "AXION // STROM", THEME_CYAN, THEME_AMBER);

    // Micro barcode stamp next to status LED
    drawGraffitiBarcode(SCREEN_WIDTH - 42, 8, THEME_DIM_CYAN);

    // Top status indicator
    D->fillCircle(SCREEN_WIDTH - 14, 11, 3, THEME_GREEN);
    D->drawCircle(SCREEN_WIDTH - 14, 11, 5, THEME_DIM_CYAN);

    int startY = 23;
    int cardH = 33;
    int spacing = 2;

    for (int i = 0; i < count; i++) {
        int y = startY + i * (cardH + spacing);
        bool isSelected = (i == selectedIndex);
        uint16_t cardBg = isSelected ? THEME_SURFACE : THEME_PANEL;
        uint16_t borderCol = isSelected ? items[i].accentColor : THEME_DIM_CYAN;

        D->fillRoundRect(6, y, SCREEN_WIDTH - 12, cardH, 4, cardBg);
        D->drawRoundRect(6, y, SCREEN_WIDTH - 12, cardH, 4, borderCol);

        if (isSelected) {
            D->fillRoundRect(6, y + 2, 4, cardH - 4, 2, items[i].accentColor);
            D->drawRoundRect(4, y - 1, SCREEN_WIDTH - 8, cardH + 2, 5, items[i].accentColor);
            D->fillRoundRect(SCREEN_WIDTH - 22, y + 8, 14, 16, 3, items[i].accentColor);
            D->setTextColor(THEME_BG, items[i].accentColor);
            D->setCursor(SCREEN_WIDTH - 17, y + 12);
            D->print(">");
        }

        uint16_t iconFg = isSelected ? items[i].accentColor : THEME_GREY;
        drawIcon(12, y + (cardH - ICON_H) / 2, items[i].iconId, iconFg, cardBg);

        D->setTextSize(1);
        D->setTextColor(isSelected ? THEME_WHITE : THEME_CYAN, cardBg);
        D->setCursor(40, y + 4);
        D->print(items[i].label);

        D->setTextColor(isSelected ? items[i].accentColor : THEME_GREY, cardBg);
        D->setCursor(40, y + 18);
        D->print(items[i].subtext);
    }
}


void DisplayUI::drawTopBar(const char* title, uint16_t accentColor, uint8_t batteryPercent) {
    D->fillRect(0, 0, SCREEN_WIDTH, 20, THEME_PANEL);
    D->drawFastHLine(0, 20, SCREEN_WIDTH, accentColor);

    // Hazard spray slashes before title
    D->drawLine(2, 17, 7, 3, THEME_AMBER);
    D->drawLine(6, 17, 11, 3, accentColor);
    D->drawPixel(14, 18, accentColor);

    D->setTextColor(accentColor, THEME_PANEL);
    D->setTextSize(1);
    D->setCursor(16, 6);
    D->print(title);

    int batX = SCREEN_WIDTH - 48;
    D->drawRect(batX, 5, 20, 10, accentColor);
    D->fillRect(batX + 20, 7, 2, 6, accentColor);
    int barWidth = (batteryPercent * 16) / 100;
    uint16_t batCol = batteryPercent > 20 ? THEME_GREEN : THEME_CRIMSON;
    D->fillRect(batX + 2, 7, barWidth, 6, batCol);
    D->setTextColor(accentColor, THEME_PANEL);
    D->setCursor(batX - 28, 6);
    D->printf("%d%%", batteryPercent);
}

void DisplayUI::drawBottomBar(const char* hint) {
    D->fillRect(0, SCREEN_HEIGHT - 16, SCREEN_WIDTH, 16, THEME_PANEL);
    D->drawFastHLine(0, SCREEN_HEIGHT - 17, SCREEN_WIDTH, THEME_DIM_CYAN);
    D->setTextColor(THEME_GREY, THEME_PANEL);
    D->setTextSize(1);
    D->setCursor(6, SCREEN_HEIGHT - 12);
    D->print(hint);
}

void DisplayUI::drawCenteredText(const char* text, int y, uint16_t color, uint8_t size) {
    D->setTextSize(size);
    D->setTextColor(color, THEME_BG);
    int w = strlen(text) * 6 * size;
    D->setCursor((SCREEN_WIDTH - w) / 2, y);
    D->print(text);
}

void DisplayUI::drawStatusBox(const char* title, const char* msg, uint16_t color) {
    int boxW = 210, boxH = 54;
    int boxX = (SCREEN_WIDTH - boxW) / 2;
    int boxY = (SCREEN_HEIGHT - boxH) / 2;
    D->fillRect(boxX, boxY, boxW, boxH, THEME_PANEL);
    D->drawRect(boxX, boxY, boxW, boxH, color);
    D->drawRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, THEME_DIM_CYAN);
    D->setTextSize(1);
    D->setTextColor(color, THEME_PANEL);
    D->setCursor(boxX + 8, boxY + 8);
    D->print(title);
    D->setTextColor(THEME_WHITE, THEME_PANEL);
    D->setCursor(boxX + 8, boxY + 28);
    D->print(msg);
}

// ============================================================
// SHARED AXION CYBER STAR LOGO (Symmetric 4-pointed star + 4 chevrons)
// ============================================================
void DisplayUI::drawAxionStarLogo(int cx, int cy, float size, float glowAlpha, bool drawChevrons) {
    if (size < 4.0f) return;

    // Outer tip radius and inner waist radius
    float R = size;
    float r = size * 0.28f; // Concave waist ratio

    // Cardinal tips: Up, Down, Left, Right
    int tipUpX = cx,             tipUpY = cy - (int)R;
    int tipDownX = cx,           tipDownY = cy + (int)R;
    int tipLeftX = cx - (int)R,  tipLeftY = cy;
    int tipRightX = cx + (int)R, tipRightY = cy;

    // Diagonal inner waist vertices
    int wURx = cx + (int)(r * 0.707f), wURy = cy - (int)(r * 0.707f);
    int wDRx = cx + (int)(r * 0.707f), wDRy = cy + (int)(r * 0.707f);
    int wDLx = cx - (int)(r * 0.707f), wDLy = cy + (int)(r * 0.707f);
    int wULx = cx - (int)(r * 0.707f), wULy = cy - (int)(r * 0.707f);

    // Celestial Facet Shading colors (Cosmic Cyan & Nebula Indigo contrast)
    uint16_t colLight = THEME_CYAN;
    uint16_t colDark  = THEME_STELLAR_BLU;
    uint16_t colEdge  = (glowAlpha > 0.5f) ? THEME_WHITE : THEME_CYAN;

    // Facet 1: Upper-Right quadrant (Starlight Cyan)
    D->fillTriangle(cx, cy, tipUpX, tipUpY, wURx, wURy, colLight);
    D->fillTriangle(cx, cy, wURx, wURy, tipRightX, tipRightY, colLight);

    // Facet 2: Lower-Right quadrant (Deep Celestial Indigo)
    D->fillTriangle(cx, cy, tipRightX, tipRightY, wDRx, wDRy, colDark);
    D->fillTriangle(cx, cy, wDRx, wDRy, tipDownX, tipDownY, colDark);

    // Facet 3: Lower-Left quadrant (Starlight Cyan)
    D->fillTriangle(cx, cy, tipDownX, tipDownY, wDLx, wDLy, colLight);
    D->fillTriangle(cx, cy, wDLx, wDLy, tipLeftX, tipLeftY, colLight);

    // Facet 4: Upper-Left quadrant (Deep Celestial Indigo)
    D->fillTriangle(cx, cy, tipLeftX, tipLeftY, wULx, wULy, colDark);
    D->fillTriangle(cx, cy, wULx, wULy, tipUpX, tipUpY, colDark);

    // Crisp high-contrast geometric outer perimeter lines
    D->drawLine(tipUpX, tipUpY, wURx, wURy, colEdge);
    D->drawLine(wURx, wURy, tipRightX, tipRightY, colEdge);
    D->drawLine(tipRightX, tipRightY, wDRx, wDRy, colEdge);
    D->drawLine(wDRx, wDRy, tipDownX, tipDownY, colEdge);
    D->drawLine(tipDownX, tipDownY, wDLx, wDLy, colEdge);
    D->drawLine(wDLx, wDLy, tipLeftX, tipLeftY, colEdge);
    D->drawLine(tipLeftX, tipLeftY, wULx, wULy, colEdge);
    D->drawLine(wULx, wULy, tipUpX, tipUpY, colEdge);

    // Center faceted division crosshair
    D->drawLine(tipUpX, tipUpY, tipDownX, tipDownY, THEME_WHITE);
    D->drawLine(tipLeftX, tipLeftY, tipRightX, tipRightY, THEME_WHITE);

    // 4 Symmetrical Celestial Chevrons pointing outward at each cardinal tip (Cyan/Silver glow)
    if (drawChevrons) {
        int chevDist = (int)(R + 7.0f);
        int chevSpan = (int)(R * 0.26f);
        if (chevSpan < 4) chevSpan = 4;
        uint16_t chevCol = (glowAlpha > 0.6f) ? THEME_CYAN : THEME_DIM_CYAN;

        // Top Chevron (pointing UP: ^)
        int ty = cy - chevDist;
        D->drawLine(cx, ty, cx - chevSpan, ty + chevSpan, chevCol);
        D->drawLine(cx, ty, cx + chevSpan, ty + chevSpan, chevCol);

        // Bottom Chevron (pointing DOWN: v)
        int by = cy + chevDist;
        D->drawLine(cx, by, cx - chevSpan, by - chevSpan, chevCol);
        D->drawLine(cx, by, cx + chevSpan, by - chevSpan, chevCol);

        // Left Chevron (pointing LEFT: <)
        int lx = cx - chevDist;
        D->drawLine(lx, cy, lx + chevSpan, cy - chevSpan, chevCol);
        D->drawLine(lx, cy, lx + chevSpan, cy + chevSpan, chevCol);

        // Right Chevron (pointing RIGHT: >)
        int rx = cx + chevDist;
        D->drawLine(rx, cy, rx - chevSpan, cy - chevSpan, chevCol);
        D->drawLine(rx, cy, rx - chevSpan, cy + chevSpan, chevCol);
    }
}


// ============================================================
// HYPERSPACE GALAXY ANIMATED BOOT SPLASH (sprite-buffered ~2.5s)
// ============================================================
void DisplayUI::drawCyberpunkBootSplash() {
    if (!s_canvas) return; // double-buffered PSRAM canvas required

    const int cx = SCREEN_WIDTH / 2;
    const int cy = 110;
    const char* logo = "AXION";
    const uint32_t T_TOTAL = 2500;
    uint32_t t0 = millis();

    // 48 Warp Star Particles radiating outward from center
    struct WarpStar {
        float angle;
        float dist;
        float speed;
        uint8_t size;
    };
    static WarpStar warpStars[48];
    static bool warpInit = false;
    if (!warpInit) {
        warpInit = true;
        for (int i = 0; i < 48; i++) {
            warpStars[i].angle = (float)(i * (6.28318f / 48.0f)) + (float)(i % 5) * 0.12f;
            warpStars[i].dist = (float)((i * 17) % 130 + 10);
            warpStars[i].speed = 1.2f + (float)(i % 7) * 0.5f;
            warpStars[i].size = (i % 3 == 0) ? 2 : 1;
        }
    }

    const char* stages[] = {
        "OCTAL PSRAM 8MB // HYPERBUS LOCK",
        "HIGH-SPEED SDMMC BUS MOUNTED",
        "OV3660 ISP SENSOR CALIBRATED",
        "RADIO & RF MATRIX ARMED",
        "AXION CYBER CORE // READY"
    };

    while (true) {
        uint32_t t = millis() - t0;
        float p = (float)t / (float)T_TOTAL;
        if (p > 1.0f) p = 1.0f;
        float elapsedSec = (float)t * 0.001f;

        s_canvas->fillScreen(THEME_BG);

        // 1. Warp Starfield: Stars streak radially outward from center
        float warpSpeedFactor = (p < 0.25f) ? (p / 0.25f * 2.8f) : 1.6f;
        for (int i = 0; i < 48; i++) {
            warpStars[i].dist += warpStars[i].speed * warpSpeedFactor * 1.5f;
            if (warpStars[i].dist > 160.0f) warpStars[i].dist = 6.0f;

            float ang = warpStars[i].angle + elapsedSec * 0.15f;
            int px = cx + (int)(cosf(ang) * warpStars[i].dist);
            int py = cy + (int)(sinf(ang) * warpStars[i].dist);

            if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                // Streak tail toward center
                float tailLen = warpStars[i].dist * 0.12f;
                int tx = cx + (int)(cosf(ang) * (warpStars[i].dist - tailLen));
                int ty = cy + (int)(sinf(ang) * (warpStars[i].dist - tailLen));

                uint16_t starCol = (warpStars[i].size == 2) ? THEME_WHITE : THEME_CYAN;
                if (warpStars[i].dist < 30.0f) starCol = THEME_DIM_CYAN;
                s_canvas->drawLine(tx, ty, px, py, starCol);
                if (warpStars[i].size == 2 && warpStars[i].dist > 45.0f) {
                    s_canvas->drawPixel(px, py, THEME_WHITE);
                }
            }
        }

        // 2. Cosmic Nebula Orbiting Wave Rings
        float wavePulse = sinf(elapsedSec * 4.5f) * 4.0f;
        s_canvas->drawCircle(cx, cy, (int)(64 + wavePulse), 0x1887); // Deep interstellar indigo
        s_canvas->drawCircle(cx, cy, (int)(52 - wavePulse * 0.6f), THEME_DIM_CYAN);
        s_canvas->drawCircle(cx, cy, (int)(40 + wavePulse * 0.8f), 0x51AF); // Soft nebula violet

        // Rotating Energy Nodes
        for (int k = 0; k < 4; k++) {
            float orbAng = elapsedSec * 2.4f + k * (6.28318f / 4.0f);
            int ox = cx + (int)(cosf(orbAng) * (52 - wavePulse * 0.6f));
            int oy = cy + (int)(sinf(orbAng) * (52 - wavePulse * 0.6f));
            s_canvas->fillCircle(ox, oy, 1, (k % 2 == 0) ? THEME_WHITE : THEME_CYAN);
        }

        // 3. Faceted Axion Star Logo: Smooth fade-in, rock-solid stable size (no pulsing)
        float starScale = 38.0f;
        if (p < 0.20f) {
            float bp = p / 0.20f;
            // Smooth natural entrance growth to final size 38
            starScale = 38.0f * (1.0f - (1.0f - bp) * (1.0f - bp));
        } else {
            starScale = 38.0f; // Firm, stable, static scale
        }

        D = s_canvas;
        drawAxionStarLogo(cx, cy, starScale, p, p > 0.15f);

        // 4. Subtle Clean Starlight Center Glint (pure soft central spark, no criss-cross lines)
        if (p > 0.18f) {
            s_canvas->fillCircle(cx, cy, 2, THEME_WHITE);
            s_canvas->drawCircle(cx, cy, 3, THEME_CYAN);
        }


        // 5. Letter-by-Letter Hologram Title Reveal
        int revealChars = 0;
        if (p > 0.34f) revealChars = (int)(((p - 0.34f) / 0.42f) * 5.0f) + 1;
        if (revealChars > 5) revealChars = 5;
        if (revealChars > 0) {
            char buf[8] = {0};
            strncpy(buf, logo, revealChars);
            s_canvas->setTextSize(3);

            // Ambient nebula violet glow behind title
            s_canvas->setTextColor(THEME_NEBULA_PURP);
            s_canvas->setCursor((SCREEN_WIDTH - 5 * 18) / 2 + 1, 183);
            s_canvas->print(buf);

            // Crisp electric cyan foreground
            s_canvas->setTextColor(THEME_CYAN);
            s_canvas->setCursor((SCREEN_WIDTH - 5 * 18) / 2, 182);
            s_canvas->print(buf);
        }

        if (p > 0.52f) {
            s_canvas->setTextSize(1);
            s_canvas->setTextColor(THEME_WHITE);
            const char* sub = "CYBER MULTI-TOOL // DUAL CORE";
            s_canvas->setCursor((SCREEN_WIDTH - (int)strlen(sub) * 6) / 2, 214);
            s_canvas->print(sub);

            s_canvas->setTextColor(THEME_GREY);
            const char* sub2 = FW_BUILD " v" FW_VERSION;
            s_canvas->setCursor((SCREEN_WIDTH - (int)strlen(sub2) * 6) / 2, 227);
            s_canvas->print(sub2);
        }

        // 6. Sleek Starlight Capsule Progress Bar
        const int barX = 26, barY = 254, barW = SCREEN_WIDTH - 52, barH = 6;
        s_canvas->drawRoundRect(barX - 2, barY - 2, barW + 4, barH + 4, 3, THEME_DIM_CYAN);
        int fillW = (int)(barW * p);
        if (fillW > 0) {
            s_canvas->fillRoundRect(barX, barY, fillW, barH, 2, THEME_STELLAR_BLU);
            int tipW = fillW > 14 ? 14 : fillW;
            s_canvas->fillRoundRect(barX + fillW - tipW, barY, tipW, barH, 2, THEME_CYAN);
            s_canvas->drawFastHLine(barX, barY + 1, fillW, THEME_WHITE); // Gloss shine stripe
        }

        // Boot Stage Label
        int stageIdx = (int)(p * 5); if (stageIdx > 4) stageIdx = 4;
        s_canvas->setTextSize(1);
        s_canvas->setTextColor((p >= 1.0f) ? THEME_CYAN : THEME_GREY);
        const char* st = (p >= 1.0f) ? "SYSTEMS ARMED // ALL CORES READY" : stages[stageIdx];
        s_canvas->setCursor((SCREEN_WIDTH - (int)strlen(st) * 6) / 2, 274);
        s_canvas->print(st);

        {
            SPIScopedLock lock;
            s_canvas->pushSprite(0, 0);
        }

        if (p >= 1.0f) break;
        vTaskDelay(pdMS_TO_TICKS(16)); // ~60 FPS target
    }
    D = &tft;
    vTaskDelay(pdMS_TO_TICKS(200));
}



// ============================================================
// CAMERA VIEWFINDER HUD (direct over live blit)
// ============================================================
void DisplayUI::drawCameraViewfinderHUD(float fps, uint64_t freeStorageMB, bool sdReady, float digitalZoom,
                                        const char* resName, const char* filterName,
                                        int brightness, int contrast, uint8_t gridMode,
                                        bool isRecording, bool isTimelapse,
                                        const char* wbName, int quickMenuIndex,
                                        uint32_t recSec, uint32_t recFrames, const char* sceneName) {
    SPIScopedLock lock;
    int cx = SCREEN_WIDTH / 2;
    int cy = SCREEN_HEIGHT / 2;

    if (gridMode == 1) { // Cyber Reticle
        tft.drawFastHLine(cx - 14, cy, 28, THEME_GREEN);
        tft.drawFastVLine(cx, cy - 14, 28, THEME_GREEN);
        tft.drawRect(cx - 24, cy - 24, 48, 48, THEME_CYAN);
        tft.drawCircle(cx, cy, 38, THEME_DIM_CYAN);
        int bSize = 14;
        tft.drawFastHLine(10, 24, bSize, THEME_CYAN);
        tft.drawFastVLine(10, 24, bSize, THEME_CYAN);
        tft.drawFastHLine(SCREEN_WIDTH - 10 - bSize, 24, bSize, THEME_CYAN);
        tft.drawFastVLine(SCREEN_WIDTH - 10, 24, bSize, THEME_CYAN);
        tft.drawFastHLine(10, SCREEN_HEIGHT - 24, bSize, THEME_CYAN);
        tft.drawFastVLine(10, SCREEN_HEIGHT - 24 - bSize, bSize, THEME_CYAN);
        tft.drawFastHLine(SCREEN_WIDTH - 10 - bSize, SCREEN_HEIGHT - 24, bSize, THEME_CYAN);
        tft.drawFastVLine(SCREEN_WIDTH - 10, SCREEN_HEIGHT - 24 - bSize, bSize, THEME_CYAN);
    } else if (gridMode == 2) { // Rule of Thirds
        int x1 = SCREEN_WIDTH / 3, x2 = (SCREEN_WIDTH * 2) / 3;
        int y1 = SCREEN_HEIGHT / 3, y2 = (SCREEN_HEIGHT * 2) / 3;
        tft.drawFastVLine(x1, 20, SCREEN_HEIGHT - 40, THEME_DIM_CYAN);
        tft.drawFastVLine(x2, 20, SCREEN_HEIGHT - 40, THEME_DIM_CYAN);
        tft.drawFastHLine(0, y1, SCREEN_WIDTH, THEME_DIM_CYAN);
        tft.drawFastHLine(0, y2, SCREEN_WIDTH, THEME_DIM_CYAN);
    } else if (gridMode == 3) { // Horizon
        tft.drawFastHLine(20, cy, SCREEN_WIDTH - 40, THEME_GREEN);
        tft.drawFastVLine(cx, cy - 8, 16, THEME_CYAN);
    }

    // Top telemetry
    tft.fillRect(0, 0, SCREEN_WIDTH, 20, THEME_PANEL);
    tft.drawFastHLine(0, 20, SCREEN_WIDTH, THEME_GREEN);
    tft.setTextColor(THEME_GREEN, THEME_PANEL);
    tft.setTextSize(1);
    tft.setCursor(6, 6);
    tft.printf("%.0fFPS %s", fps, resName);

    if (isRecording) {
        bool blink = (millis() / 500) % 2;
        if (blink) { tft.fillCircle(104, 10, 4, THEME_CRIMSON); }
        tft.setTextColor(THEME_CRIMSON, THEME_PANEL);
        tft.setCursor(112, 6);
        tft.printf("REC %02u:%02u", (unsigned)(recSec / 60), (unsigned)(recSec % 60));
    } else if (isTimelapse) {
        bool blink = (millis() / 700) % 2;
        if (blink) tft.fillCircle(104, 10, 4, THEME_AMBER);
        tft.setTextColor(THEME_AMBER, THEME_PANEL);
        tft.setCursor(112, 6);
        tft.print("TIMELAPSE");
    } else {
        tft.setTextColor(THEME_CYAN, THEME_PANEL);
        tft.setCursor(100, 6);
        tft.printf("%s %.1fx", filterName, digitalZoom);
    }

    tft.setCursor(SCREEN_WIDTH - 60, 6);
    tft.setTextColor(sdReady ? THEME_WHITE : THEME_CRIMSON, THEME_PANEL);
    tft.printf("%s%lluM", sdReady ? "SD " : "ERR", (unsigned long long)freeStorageMB);

    // Bottom prompt + scene/wb strip
    tft.fillRect(0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, 20, THEME_PANEL);
    tft.drawFastHLine(0, SCREEN_HEIGHT - 21, SCREEN_WIDTH, THEME_CYAN);
    tft.setTextColor(THEME_WHITE, THEME_PANEL);
    tft.setTextSize(1);
    tft.setCursor(4, SCREEN_HEIGHT - 14);
    if (quickMenuIndex >= 0) {
        tft.print("U/D:Nav  L/R:Adjust  CLICK:Select");
    } else {
        tft.printf("SNAP:Photo CLICK:Menu HOLD:Rec [%s]", sceneName);
    }
}

void DisplayUI::drawCameraQuickMenu(int selectedIndex, const char* items[], int itemCount) {
    SPIScopedLock lock;
    int boxW = 184;
    int rowH = 17;
    int boxH = itemCount * rowH + 14;
    int boxX = (SCREEN_WIDTH - boxW) / 2;
    int boxY = (SCREEN_HEIGHT - boxH) / 2;

    tft.fillRoundRect(boxX, boxY, boxW, boxH, 4, THEME_PANEL);
    tft.drawRoundRect(boxX, boxY, boxW, boxH, 4, THEME_CYAN);
    tft.drawRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 3, THEME_DIM_CYAN);

    tft.setTextSize(1);
    for (int i = 0; i < itemCount; i++) {
        int y = boxY + 8 + (i * rowH);
        bool isSel = (i == selectedIndex);
        if (isSel) {
            tft.fillRoundRect(boxX + 4, y - 2, boxW - 8, rowH - 1, 2, THEME_SURFACE);
            tft.drawRoundRect(boxX + 4, y - 2, boxW - 8, rowH - 1, 2, THEME_GREEN);
            tft.setTextColor(THEME_GREEN, THEME_SURFACE);
            tft.setCursor(boxX + 8, y + 2);
            tft.print("> ");
        } else {
            tft.setTextColor(THEME_WHITE, THEME_PANEL);
            tft.setCursor(boxX + 16, y + 2);
        }
        tft.print(items[i]);
    }
}

void DisplayUI::drawCameraQuickValue(const char* label, const char* value) {
    SPIScopedLock lock;
    int w = 150, h = 22;
    int x = (SCREEN_WIDTH - w) / 2;
    int y = SCREEN_HEIGHT - 48;
    tft.fillRoundRect(x, y, w, h, 4, THEME_PANEL);
    tft.drawRoundRect(x, y, w, h, 4, THEME_AMBER);
    tft.setTextSize(1);
    tft.setTextColor(THEME_WHITE, THEME_PANEL);
    tft.setCursor(x + 8, y + 7);
    tft.print(label);
    tft.setTextColor(THEME_AMBER, THEME_PANEL);
    int vw = strlen(value) * 6;
    tft.setCursor(x + w - vw - 8, y + 7);
    tft.print(value);
}

void DisplayUI::showCaptureFreezeFlash(const char* savedPath) {
    SPIScopedLock lock;
    tft.startWrite();

    // 1. Dual white-hot flash blit
    tft.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, THEME_WHITE);
    tft.drawRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, THEME_WHITE);
    tft.drawRect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, THEME_CYAN);

    // 2. Camera mechanical shutter iris animation (geometric shutter blades closing)
    int cx = SCREEN_WIDTH / 2;
    int cy = SCREEN_HEIGHT / 2;
    for (int r = 16; r <= 60; r += 14) {
        tft.drawCircle(cx, cy, r, THEME_WHITE);
        tft.drawFastHLine(cx - r, cy, r * 2, THEME_CYAN);
        tft.drawFastVLine(cx, cy - r, r * 2, THEME_CYAN);
    }
    tft.endWrite();

    // 3. Render the actual saved photo centered as instant review preview
    if (savedPath && SD_MMC.exists(savedPath)) {
        tft.startWrite();
        tft.setClipRect(20, 54, SCREEN_WIDTH - 40, 192);
        tft.drawJpgFile(SD_MMC, savedPath, 20, 54, SCREEN_WIDTH - 40, 192, 0, 0, 0.0f, 0.0f, lgfx::datum_t::middle_center);
        tft.clearClipRect();
        tft.drawRoundRect(19, 53, SCREEN_WIDTH - 38, 194, 4, THEME_CYAN);
        tft.endWrite();
    }

    // 4. Starlight confirmation badge
    int toastW = 196, toastH = 26;
    int toastX = (SCREEN_WIDTH - toastW) / 2;
    int toastY = SCREEN_HEIGHT - 54;
    tft.fillRoundRect(toastX, toastY, toastW, toastH, 4, THEME_PANEL);
    tft.drawRoundRect(toastX, toastY, toastW, toastH, 4, THEME_GREEN);
    tft.fillCircle(toastX + 14, toastY + 13, 4, THEME_GREEN);
    tft.setTextColor(THEME_GREEN, THEME_PANEL);
    tft.setTextSize(1);
    tft.setCursor(toastX + 26, toastY + 9);
    tft.print("CAPTURED // SAVED TO SD");
    delay(380);
}

void DisplayUI::showVideoRecordAnimation(bool starting) {
    SPIScopedLock lock;
    tft.startWrite();
    int cx = SCREEN_WIDTH / 2;
    int cy = SCREEN_HEIGHT / 2;

    if (starting) {
        // Red recording pulse ring expand
        for (int r = 10; r <= 52; r += 8) {
            tft.drawCircle(cx, cy, r, THEME_CRIMSON);
            tft.drawCircle(cx, cy, r + 1, THEME_CRIMSON);
        }
        // Big REC circle
        tft.fillCircle(cx, cy, 22, THEME_CRIMSON);
        tft.fillCircle(cx, cy, 8, THEME_WHITE);

        int badgeW = 160, badgeH = 24;
        int badgeX = (SCREEN_WIDTH - badgeW) / 2;
        int badgeY = cy + 40;
        tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, THEME_PANEL);
        tft.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 4, THEME_CRIMSON);
        tft.setTextColor(THEME_WHITE, THEME_PANEL);
        tft.setTextSize(1);
        tft.setCursor(badgeX + 18, badgeY + 8);
        tft.print("REC // RECORDING START");
    } else {
        // Stop record square badge
        tft.drawRoundRect(cx - 30, cy - 30, 60, 60, 6, THEME_AMBER);
        tft.fillRect(cx - 14, cy - 14, 28, 28, THEME_AMBER);

        int badgeW = 160, badgeH = 24;
        int badgeX = (SCREEN_WIDTH - badgeW) / 2;
        int badgeY = cy + 40;
        tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, THEME_PANEL);
        tft.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 4, THEME_AMBER);
        tft.setTextColor(THEME_AMBER, THEME_PANEL);
        tft.setTextSize(1);
        tft.setCursor(badgeX + 16, badgeY + 8);
        tft.print("SAVED // VIDEO ENCODED");
    }
    tft.endWrite();
    delay(320);
}

// ============================================================
// GALLERY (buffered)
// ============================================================
void DisplayUI::drawGalleryScreen(const char* imgPath, int imgIndex, int totalImgs, bool fileValid, bool isVideo) {
    drawTopBar(isVideo ? "GALLERY // VIDEO" : "GALLERY // PHOTO", THEME_CYAN);

    if (totalImgs == 0) {
        drawCenteredText("NO MEDIA ON SD CARD", SCREEN_HEIGHT / 2 - 10, THEME_AMBER, 1);
        drawCenteredText("Capture photos in Camera mode", SCREEN_HEIGHT / 2 + 8, THEME_GREY, 1);
        drawBottomBar("MENU:Home");
        return;
    }

    int viewY = 26, viewH = 244;
    D->drawRect(6, viewY, SCREEN_WIDTH - 12, viewH, THEME_DIM_CYAN);

    if (isVideo) {
        // Video entries can't be thumbnailed cheaply; show a film-strip card with a play glyph
        D->fillRect(8, viewY + 2, SCREEN_WIDTH - 16, viewH - 4, THEME_SURFACE);
        for (int sx = 14; sx < SCREEN_WIDTH - 14; sx += 20) {
            D->fillRect(sx, viewY + 10, 12, 9, THEME_PANEL);
            D->fillRect(sx, viewY + viewH - 20, 12, 9, THEME_PANEL);
        }
        int pcx = SCREEN_WIDTH / 2, pcy = viewY + viewH / 2;
        D->fillCircle(pcx, pcy, 26, THEME_PANEL);
        D->drawCircle(pcx, pcy, 26, THEME_GREEN);
        D->fillTriangle(pcx - 9, pcy - 13, pcx - 9, pcy + 13, pcx + 15, pcy, THEME_GREEN);
    } else if (fileValid) {
        D->drawJpgFile(SD_MMC, imgPath, 8, viewY + 2, SCREEN_WIDTH - 16, viewH - 4,
                       0, 0, 0.0f, 0.0f, lgfx::datum_t::middle_center);
    } else {
        drawCenteredText("FILE CORRUPT OR MISSING", SCREEN_HEIGHT / 2, THEME_CRIMSON, 1);
    }

    char info[64];
    snprintf(info, sizeof(info), "[%d/%d] %s", imgIndex + 1, totalImgs, imgPath);
    D->fillRect(0, SCREEN_HEIGHT - 32, SCREEN_WIDTH, 16, THEME_PANEL);
    D->setTextColor(THEME_GREEN, THEME_PANEL);
    D->setTextSize(1);
    D->setCursor(6, SCREEN_HEIGHT - 28);
    D->print(info);

    drawBottomBar(isVideo ? "CLICK:Play  L/R:Browse  MENU:Home"
                          : "L/R:Browse  CLICK:Delete  MENU:Home");
}

void DisplayUI::drawVideoFrame(const uint8_t* jpg, size_t len, int frameIdx, int totalFrames, bool playing) {
    if (jpg && len > 0) {
        D->drawJpg(jpg, len, 0, 22, SCREEN_WIDTH, SCREEN_HEIGHT - 60, 0, 0, 0.0f, 0.0f, lgfx::datum_t::middle_center);
    }
    D->fillRect(0, 0, SCREEN_WIDTH, 20, THEME_PANEL);
    D->drawFastHLine(0, 20, SCREEN_WIDTH, THEME_CRIMSON);
    D->setTextColor(THEME_CRIMSON, THEME_PANEL);
    D->setTextSize(1);
    D->setCursor(6, 6);
    D->printf("%s  FRAME %d/%d", playing ? "> PLAY" : "|| PAUSE", frameIdx + 1, totalFrames);

    int barY = SCREEN_HEIGHT - 34;
    D->fillRect(6, barY, SCREEN_WIDTH - 12, 4, THEME_SURFACE);
    int w = totalFrames > 0 ? ((SCREEN_WIDTH - 12) * (frameIdx + 1)) / totalFrames : 0;
    D->fillRect(6, barY, w, 4, THEME_GREEN);
    drawBottomBar("CLICK:Play/Pause  L/R:Seek  MENU:Back");
}

// ============================================================
// SCROLLABLE SETTINGS (buffered - Black & White High Contrast Theme)
// ============================================================
void DisplayUI::drawSettingsMenu(int selectedIndex, const char* names[], const char* values[],
                                 const bool* isHeader, int itemCount, int scrollOffset, int visibleRows) {
    // Header in sleek silver / pure starlight white
    drawTopBar("SYSTEM SETTINGS", THEME_BW_WHITE);

    int startY = 26;
    int rowH = 24;

    for (int row = 0; row < visibleRows; row++) {
        int i = scrollOffset + row;
        if (i >= itemCount) break;
        int y = startY + row * rowH;
        bool header = isHeader && isHeader[i];

        if (header) {
            D->setTextColor(THEME_BW_WHITE, THEME_BW_BG);
            D->setTextSize(1);
            D->setCursor(10, y + 8);
            D->print(names[i]);
            // Subtle silver-grey divider line
            D->drawFastHLine(10, y + 20, SCREEN_WIDTH - 20, THEME_BW_BORDER);
            continue;
        }

        bool isSel = (i == selectedIndex);
        uint16_t cardBg = isSel ? THEME_BW_CARD_SEL : THEME_BW_SURFACE;
        D->fillRoundRect(6, y, SCREEN_WIDTH - 16, rowH - 2, 4, cardBg);
        D->drawRoundRect(6, y, SCREEN_WIDTH - 16, rowH - 2, 4, isSel ? THEME_BW_WHITE : THEME_BW_BORDER);
        if (isSel) D->fillRoundRect(6, y + 2, 4, rowH - 6, 2, THEME_BW_WHITE);

        D->setTextSize(1);
        D->setTextColor(isSel ? THEME_BW_WHITE : THEME_BW_SILVER, cardBg);
        D->setCursor(14, y + 7);
        D->print(names[i]);

        if (values[i] && values[i][0]) {
            D->setTextColor(isSel ? THEME_BW_WHITE : THEME_BW_MUTED, cardBg);
            int valW = strlen(values[i]) * 6;
            D->setCursor(SCREEN_WIDTH - 20 - valW, y + 7);
            D->print(values[i]);
        }
    }

    // High-contrast clean scrollbar
    if (itemCount > visibleRows) {
        int trackX = SCREEN_WIDTH - 5, trackY = startY, trackH = visibleRows * rowH;
        D->drawFastVLine(trackX, trackY, trackH, THEME_BW_BORDER);
        int thumbH = trackH * visibleRows / itemCount;
        int thumbY = trackY + (trackH - thumbH) * scrollOffset / (itemCount - visibleRows);
        D->fillRect(trackX - 1, thumbY, 3, thumbH, THEME_BW_WHITE);
    }

    drawBottomBar("U/D:Nav  L/R:Adjust  CLICK:Action  MENU:Save");
}


// ============================================================
// SD FILE EXPLORER (buffered)
// ============================================================
static uint8_t fileIconFor(const SdEntry& e) {
    if (e.isDir) return ICON_GENERAL_APPS;
    const char* dot = strrchr(e.name, '.');
    if (!dot) return ICON_BLANK;
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return ICON_CAMERA;
    if (strcasecmp(dot, ".avi") == 0) return ICON_CAMERA;
    if (strcasecmp(dot, ".ir") == 0)  return ICON_IR;
    if (strcasecmp(dot, ".sub") == 0) return ICON_SUBGHZ;
    if (strcasecmp(dot, ".pcap") == 0) return ICON_PACKET_MONITOR;
    return ICON_BLANK;
}

static void humanSize(uint64_t bytes, char* out, size_t n) {
    if (bytes >= 1024ULL * 1024ULL) snprintf(out, n, "%.1fM", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024ULL)      snprintf(out, n, "%.1fK", bytes / 1024.0);
    else                            snprintf(out, n, "%uB", (unsigned)bytes);
}

void DisplayUI::drawFileExplorer(const char* path, const SdEntry* entries, int count,
                                 int selectedIndex, int scrollOffset, uint64_t freeMB, uint64_t totalMB) {
    drawTopBar("FILE EXPLORER", THEME_CYAN);

    D->setTextColor(THEME_GREY, THEME_BG);
    D->setTextSize(1);
    D->setCursor(6, 24);
    D->printf("PATH: %s", path);

    int startY = 36;
    int rowH = 22;
    int visibleRows = 10;

    if (count == 0) {
        drawCenteredText("[ EMPTY FOLDER ]", 150, THEME_GREY, 1);
    }

    for (int row = 0; row < visibleRows; row++) {
        int i = scrollOffset + row;
        if (i >= count) break;
        int y = startY + row * rowH;
        bool isSel = (i == selectedIndex);
        uint16_t bg = isSel ? THEME_SURFACE : THEME_PANEL;

        D->fillRect(6, y, SCREEN_WIDTH - 14, rowH - 2, bg);
        if (isSel) D->drawRect(6, y, SCREEN_WIDTH - 14, rowH - 2, THEME_GREEN);

        drawIcon(10, y + 1, fileIconFor(entries[i]),
                 isSel ? THEME_GREEN : THEME_GREY, bg); // 22px tall fits row

        D->setTextColor(entries[i].isDir ? THEME_CYAN : (isSel ? THEME_WHITE : THEME_GREY), bg);
        D->setCursor(36, y + 7);
        char nm[22];
        strncpy(nm, entries[i].name, 21); nm[21] = '\0';
        D->print(nm);

        if (!entries[i].isDir) {
            char sz[12];
            humanSize(entries[i].size, sz, sizeof(sz));
            D->setTextColor(THEME_AMBER, bg);
            int sw = strlen(sz) * 6;
            D->setCursor(SCREEN_WIDTH - 16 - sw, y + 7);
            D->print(sz);
        }
    }

    if (count > visibleRows) {
        int trackY = startY, trackH = visibleRows * rowH;
        D->drawFastVLine(SCREEN_WIDTH - 5, trackY, trackH, THEME_DIM_CYAN);
        int thumbH = trackH * visibleRows / count;
        int thumbY = trackY + (trackH - thumbH) * scrollOffset / (count - visibleRows);
        D->fillRect(SCREEN_WIDTH - 6, thumbY, 3, thumbH, THEME_CYAN);
    }

    // Storage bar
    int by = SCREEN_HEIGHT - 34;
    D->setTextColor(THEME_GREY, THEME_PANEL);
    D->fillRect(0, by, SCREEN_WIDTH, 18, THEME_PANEL);
    char stor[40];
    snprintf(stor, sizeof(stor), "FREE %lluMB / %lluMB", (unsigned long long)freeMB, (unsigned long long)totalMB);
    D->setCursor(6, by + 5);
    D->print(stor);
    int usedPct = totalMB > 0 ? (int)(((totalMB - freeMB) * 100) / totalMB) : 0;
    D->drawRect(SCREEN_WIDTH - 70, by + 4, 60, 9, THEME_DIM_CYAN);
    D->fillRect(SCREEN_WIDTH - 69, by + 5, (58 * usedPct) / 100, 7, usedPct > 90 ? THEME_CRIMSON : THEME_CYAN);

    drawBottomBar("CLICK:Open  L:Up  CLICK-HOLD:Del  MENU:Home");
}

// ============================================================
// INFO / ABOUT SCREEN (buffered)
// ============================================================
void DisplayUI::drawInfoScreen(const char* title, uint16_t accent, const char* lines[], int count, int storagePct) {
    drawTopBar(title, accent);
    int y = 32;
    for (int i = 0; i < count; i++) {
        if (!lines[i]) { y += 6; continue; }
        bool head = (lines[i][0] == '#');
        if (head) {
            D->setTextColor(THEME_AMBER, THEME_BG);
            D->setCursor(8, y);
            D->print(lines[i] + 1);
            D->drawFastHLine(8, y + 10, SCREEN_WIDTH - 16, THEME_DIM_CYAN);
            y += 16;
        } else {
            D->setTextColor(THEME_WHITE, THEME_BG);
            D->setTextSize(1);
            D->setCursor(12, y);
            D->print(lines[i]);
            y += 14;
        }
    }
    if (storagePct >= 0) {
        int by = SCREEN_HEIGHT - 36;
        D->setTextColor(THEME_GREY, THEME_BG);
        D->setCursor(8, by - 2);
        D->print("SD STORAGE");
        D->drawRect(8, by + 10, SCREEN_WIDTH - 16, 12, THEME_DIM_CYAN);
        D->fillRect(9, by + 11, ((SCREEN_WIDTH - 18) * storagePct) / 100, 10,
                    storagePct > 90 ? THEME_CRIMSON : THEME_GREEN);
    }
    drawBottomBar("MENU:Home");
}

void DisplayUI::drawGuideScreen(const char* categoryTitle, uint16_t accent, int pageIdx, int totalPages,
                                const char* overview, const char* controls[], int controlCount,
                                const char* notes[], int noteCount) {
    char titleBuf[48];
    snprintf(titleBuf, sizeof(titleBuf), "GUIDE [%d/%d] %s", pageIdx + 1, totalPages, categoryTitle);
    drawTopBar(titleBuf, accent);

    int y = 26;

    // Overview Section Box
    D->fillRoundRect(6, y, SCREEN_WIDTH - 12, 48, 4, THEME_PANEL);
    D->drawRoundRect(6, y, SCREEN_WIDTH - 12, 48, 4, accent);
    D->setTextColor(accent, THEME_PANEL);
    D->setCursor(12, y + 5);
    D->print("FUNCTION OVERVIEW");

    D->setTextColor(THEME_WHITE, THEME_PANEL);
    // Draw two lines of overview text with line-splitting
    const char* pBreak = strchr(overview, '|');
    if (pBreak) {
        char line1[42];
        size_t l1len = pBreak - overview;
        if (l1len >= sizeof(line1)) l1len = sizeof(line1) - 1;
        strncpy(line1, overview, l1len);
        line1[l1len] = '\0';
        D->setCursor(12, y + 18);
        D->print(line1);
        D->setCursor(12, y + 31);
        D->print(pBreak + 1);
    } else {
        D->setCursor(12, y + 20);
        D->print(overview);
    }
    y += 54;

    // Controls Section
    D->fillRoundRect(6, y, SCREEN_WIDTH - 12, 102, 4, THEME_SURFACE);
    D->drawRoundRect(6, y, SCREEN_WIDTH - 12, 102, 4, THEME_DIM_CYAN);
    D->setTextColor(THEME_AMBER, THEME_SURFACE);
    D->setCursor(12, y + 5);
    D->print("OPERATING CONTROLS");
    D->drawFastHLine(12, y + 15, SCREEN_WIDTH - 24, THEME_DIM_CYAN);

    int cy = y + 20;
    for (int i = 0; i < controlCount && i < 5; i++) {
        D->setTextColor(THEME_CYAN, THEME_SURFACE);
        const char* colon = strchr(controls[i], ':');
        if (colon) {
            char key[20];
            size_t klen = colon - controls[i];
            if (klen >= sizeof(key)) klen = sizeof(key) - 1;
            strncpy(key, controls[i], klen);
            key[klen] = '\0';
            D->setCursor(12, cy);
            D->print(key);
            D->setTextColor(THEME_WHITE, THEME_SURFACE);
            D->setCursor(12 + (klen * 6) + 4, cy);
            D->print(colon);
        } else {
            D->setCursor(12, cy);
            D->print(controls[i]);
        }
        cy += 15;
    }
    y += 108;

    // Tips & Operational Notes Box
    D->fillRoundRect(6, y, SCREEN_WIDTH - 12, 82, 4, THEME_PANEL);
    D->drawRoundRect(6, y, SCREEN_WIDTH - 12, 82, 4, THEME_DIM_CYAN);
    D->setTextColor(THEME_GREEN, THEME_PANEL);
    D->setCursor(12, y + 5);
    D->print("OPERATIONAL DETAILS & HARDWARE");
    D->drawFastHLine(12, y + 15, SCREEN_WIDTH - 24, THEME_DIM_CYAN);

    int ny = y + 20;
    for (int i = 0; i < noteCount && i < 4; i++) {
        D->fillCircle(15, ny + 3, 2, THEME_GREEN);
        D->setTextColor(THEME_WHITE, THEME_PANEL);
        D->setCursor(22, ny);
        D->print(notes[i]);
        ny += 14;
    }

    drawBottomBar("< PREV  [CLICK:Next]  NEXT >  MENU:Exit");
}

void DisplayUI::drawConfirm(const char* title, const char* msg, bool yesSelected) {
    int boxW = 200, boxH = 90;
    int boxX = (SCREEN_WIDTH - boxW) / 2;
    int boxY = (SCREEN_HEIGHT - boxH) / 2;
    D->fillRoundRect(boxX, boxY, boxW, boxH, 6, THEME_PANEL);
    D->drawRoundRect(boxX, boxY, boxW, boxH, 6, THEME_CRIMSON);
    D->setTextColor(THEME_CRIMSON, THEME_PANEL);
    D->setTextSize(1);
    D->setCursor(boxX + 10, boxY + 10);
    D->print(title);
    D->setTextColor(THEME_WHITE, THEME_PANEL);
    D->setCursor(boxX + 10, boxY + 30);
    D->print(msg);

    int bw = 70, bh = 24, byy = boxY + boxH - 32;
    // NO
    D->fillRoundRect(boxX + 14, byy, bw, bh, 4, yesSelected ? THEME_SURFACE : THEME_GREEN);
    D->setTextColor(yesSelected ? THEME_GREY : THEME_BG, yesSelected ? THEME_SURFACE : THEME_GREEN);
    D->setCursor(boxX + 14 + 26, byy + 8);
    D->print("NO");
    // YES
    D->fillRoundRect(boxX + boxW - 14 - bw, byy, bw, bh, 4, yesSelected ? THEME_CRIMSON : THEME_SURFACE);
    D->setTextColor(yesSelected ? THEME_WHITE : THEME_GREY, yesSelected ? THEME_CRIMSON : THEME_SURFACE);
    D->setCursor(boxX + boxW - 14 - bw + 22, byy + 8);
    D->print("YES");
}

void DisplayUI::drawNamingKeyboardDialog(const char* currentText, int keyRow, int keyCol,
                                         const char* fixedFormat, const char* title, const char* hint) {
    (void)hint; // Unused - bottom hint bar removed per design
    // Slim Bottom-Docked Keyboard Panel (116px tall, sits right at the bottom: y = 202 to 318)
    int dw = SCREEN_WIDTH - 8; // 232px wide
    int dh = 116;              // compact height, leaves 202px of viewfinder/screen clear!
    int dx = 4;
    int dy = SCREEN_HEIGHT - dh - 2; // y = 202

    // Semi-translucent dark slate backing card with thin electric cyan border
    D->fillRoundRect(dx, dy, dw, dh, 5, THEME_PANEL);
    D->drawRoundRect(dx, dy, dw, dh, 5, THEME_CYAN);

    // Slim Title & Badge Bar (y = dy + 3)
    D->setTextSize(1);
    D->setTextColor(THEME_CYAN, THEME_PANEL);
    D->setCursor(dx + 8, dy + 5);
    D->print(title ? title : "TEXT INPUT");

    if (fixedFormat && strlen(fixedFormat) > 0) {
        char fmtBadge[18];
        snprintf(fmtBadge, sizeof(fmtBadge), "[.%s LOCKED]", fixedFormat);
        D->setTextColor(THEME_AMBER, THEME_PANEL);
        int fbw = strlen(fmtBadge) * 6;
        D->setCursor(dx + dw - fbw - 8, dy + 5);
        D->print(fmtBadge);
    }

    // Symmetrical 4-Row 10-Key Matrix (Keys: 20x16px, 3px padding, y = dy + 37)
    // All 4 rows are exactly 10 keys wide:
    static const char* kbdRows[4] = {
        "1234567890",
        "QWERTYUIOP",
        "ASDFGHJKL_",
        "ZXCVBNM.-/"
    };

    // Header Toolbar Function Buttons: [DEL] and [SAVE] on right side of textbox
    // tbX = dx + 6, tbY = dy + 16, tbW = dw - 78 (leaves 70px for DEL and SAVE buttons)
    int tbX = dx + 6, tbY = dy + 16, tbW = dw - 78, tbH = 18;
    D->fillRoundRect(tbX, tbY, tbW, tbH, 3, THEME_SURFACE);
    D->drawRoundRect(tbX, tbY, tbW, tbH, 3, THEME_DIM_CYAN);

    D->setTextColor(THEME_WHITE, THEME_SURFACE);
    D->setCursor(tbX + 6, tbY + 5);
    D->print(currentText);

    // Blinking / active cursor tick
    int curX = tbX + 6 + (strlen(currentText) * 6);
    D->fillRect(curX, tbY + 4, 2, 10, THEME_CYAN);

    // Fixed format extension suffix (if present)
    if (fixedFormat && strlen(fixedFormat) > 0) {
        char extBuf[10];
        snprintf(extBuf, sizeof(extBuf), ".%s", fixedFormat);
        D->setTextColor(THEME_GREY, THEME_SURFACE);
        D->setCursor(curX + 4, tbY + 5);
        D->print(extBuf);
    }

    // Top Right Action Buttons: [DEL] (32px) and [SAVE] (36px)
    int delX = tbX + tbW + 4;
    int delW = 32;
    bool delSel = (keyRow == -1 && keyCol == 0);
    D->fillRoundRect(delX, tbY, delW, tbH, 3, delSel ? THEME_CRIMSON : THEME_SURFACE);
    D->drawRoundRect(delX, tbY, delW, tbH, 3, delSel ? THEME_WHITE : THEME_CRIMSON);
    D->setTextColor(delSel ? THEME_WHITE : THEME_CRIMSON, delSel ? THEME_CRIMSON : THEME_SURFACE);
    D->setCursor(delX + 8, tbY + 5);
    D->print("DEL");

    int saveX = delX + delW + 3;
    int saveW = 34;
    bool saveSel = (keyRow == -1 && keyCol == 1);
    D->fillRoundRect(saveX, tbY, saveW, tbH, 3, saveSel ? THEME_GREEN : THEME_SURFACE);
    D->drawRoundRect(saveX, tbY, saveW, tbH, 3, saveSel ? THEME_WHITE : THEME_GREEN);
    D->setTextColor(saveSel ? THEME_BG : THEME_GREEN, saveSel ? THEME_GREEN : THEME_SURFACE);
    D->setCursor(saveX + 6, tbY + 5);
    D->print("SAVE");

    // Keys Matrix: 10 columns x 4 rows
    int kY = dy + 38;
    int keyW = 20, keyH = 16, kPad = 3;

    for (int r = 0; r < 4; r++) {
        int rStartX = dx + 5;
        for (int c = 0; c < 10; c++) {
            int kx = rStartX + c * (keyW + kPad);
            int ky = kY + r * (keyH + kPad);
            bool isSel = (r == keyRow && c == keyCol);

            uint16_t kBg = isSel ? THEME_CYAN : THEME_SURFACE;
            uint16_t kFg = isSel ? THEME_BG : THEME_WHITE;
            D->fillRoundRect(kx, ky, keyW, keyH, 2, kBg);
            if (isSel) D->drawRoundRect(kx, ky, keyW, keyH, 2, THEME_WHITE);

            D->setTextColor(kFg, kBg);
            D->setCursor(kx + 7, ky + 4);
            D->print(kbdRows[r][c]);
        }
    }
}

// ============================================================
// NOTEPAD TEXT EDITOR SCREEN (REVAMPED CYBER EDITOR)
// ============================================================
void DisplayUI::drawNotepadEditor(const char* fileName, const char* content, int keyRow, int keyCol,
                                 bool namingStep, const char* nameBuffer, uint32_t toastUntil,
                                 bool exitPrompt, int promptChoice) {
    // 1. Top HUD
    drawTopBar("NOTEPAD // CYBER EDITOR", THEME_CYAN);

    // 2. Editor Document Page Surface (y = 22 to 194, h = 172px)
    int docX = 4, docY = 22, docW = SCREEN_WIDTH - 8, docH = 172;
    D->fillRoundRect(docX, docY, docW, docH, 4, THEME_SURFACE);
    D->drawRoundRect(docX, docY, docW, docH, 4, THEME_DIM_CYAN);

    // File header ribbon inside document
    int charCount = content ? strlen(content) : 0;
    D->setTextSize(1);
    D->setTextColor(THEME_CYAN, THEME_SURFACE);
    D->setCursor(docX + 6, docY + 4);
    D->print("FILE: ");
    D->setTextColor(THEME_WHITE, THEME_SURFACE);
    D->print(fileName ? fileName : "note_001.TXT");

    char statInfo[32];
    snprintf(statInfo, sizeof(statInfo), "%dB  C:%d", charCount, charCount);
    D->setTextColor(THEME_GREY, THEME_SURFACE);
    int statW = strlen(statInfo) * 6;
    D->setCursor(docX + docW - statW - 6, docY + 4);
    D->print(statInfo);

    // Subtle header separator line
    D->drawFastHLine(docX + 2, docY + 15, docW - 4, THEME_DIM_CYAN);

    // Margin line for line numbering
    D->drawFastVLine(docX + 22, docY + 16, docH - 18, 0x0862);

    int textX = docX + 26;
    int textY = docY + 19;
    int lineSpacing = 12;
    int maxLines = 12;
    int curLine = 0;

    int cx = textX;
    int cy = textY;

    // Draw initial line number "01"
    D->setTextColor(THEME_DIM_CYAN, THEME_SURFACE);
    D->setCursor(docX + 5, textY);
    D->print("01");

    if (!content || strlen(content) == 0) {
        D->setTextColor(THEME_DIM_CYAN, THEME_SURFACE);
        D->setCursor(textX, textY);
        D->print("[ CLEAN NEW FILE - TYPE BELOW ]");
    } else {
        D->setTextColor(THEME_WHITE, THEME_SURFACE);
        int len = strlen(content);
        for (int i = 0; i < len; i++) {
            char ch = content[i];
            if (ch == '\n') {
                curLine++;
                cx = textX;
                cy += lineSpacing;
                if (curLine >= maxLines) break;
                // Draw next line number
                char lnum[8];
                snprintf(lnum, sizeof(lnum), "%02d", curLine + 1);
                D->setTextColor(THEME_DIM_CYAN, THEME_SURFACE);
                D->setCursor(docX + 5, cy);
                D->print(lnum);
                D->setTextColor(THEME_WHITE, THEME_SURFACE);
                continue;
            }
            if (cx > docX + docW - 10) {
                curLine++;
                cx = textX;
                cy += lineSpacing;
                if (curLine >= maxLines) break;
                char lnum[8];
                snprintf(lnum, sizeof(lnum), "%02d", curLine + 1);
                D->setTextColor(THEME_DIM_CYAN, THEME_SURFACE);
                D->setCursor(docX + 5, cy);
                D->print(lnum);
                D->setTextColor(THEME_WHITE, THEME_SURFACE);
            }
            D->drawChar(ch, cx, cy);
            cx += 6;
        }
    }

    // Blinking active cursor
    if (!namingStep && (millis() / 350) % 2 == 0) {
        D->fillRect(cx, cy + 8, 5, 2, THEME_CYAN);
    }

    // Save Toast Notification overlay
    if (toastUntil > millis()) {
        int tw = 180, th = 30;
        int tx = (SCREEN_WIDTH - tw) / 2;
        int ty = docY + 50;
        D->fillRoundRect(tx, ty, tw, th, 4, THEME_PANEL);
        D->drawRoundRect(tx, ty, tw, th, 4, THEME_GREEN);
        D->setTextColor(THEME_GREEN, THEME_PANEL);
        D->setTextSize(1);
        D->setCursor(tx + 12, ty + 10);
        D->print("FILE SAVED TO /NOTEPAD");
    }

    // 3. Virtual Cyber Keyboard Docked at bottom (y = 196 to 300, h = 104)
    if (namingStep) {
        drawNamingKeyboardDialog(nameBuffer ? nameBuffer : "note_001", keyRow, keyCol, "TXT", "SAVE TO /NOTEPAD");
        return;
    }

    int kbX = 4, kbY = 196, kbW = SCREEN_WIDTH - 8, kbH = 104;
    D->fillRoundRect(kbX, kbY, kbW, kbH, 4, THEME_PANEL);
    D->drawRoundRect(kbX, kbY, kbW, kbH, 4, THEME_CYAN);

    // Row -1: Action Bar (5 fast action keys: [DEL], [SPACE], [ENTER], [NEW], [SAVE])
    const char* actLabels[5] = {"DEL", "SPACE", "ENTER", "NEW", "SAVE"};
    uint16_t actColors[5] = {THEME_CRIMSON, THEME_CYAN, THEME_AMBER, THEME_GREY, THEME_GREEN};
    int actW[5] = {38, 44, 44, 38, 46};
    int curAx = kbX + 6;

    for (int a = 0; a < 5; a++) {
        bool sel = (keyRow == -1 && keyCol == a);
        uint16_t col = actColors[a];
        uint16_t bg = sel ? col : THEME_SURFACE;
        uint16_t fg = sel ? THEME_BG : col;

        D->fillRoundRect(curAx, kbY + 4, actW[a], 16, 2, bg);
        D->drawRoundRect(curAx, kbY + 4, actW[a], 16, 2, sel ? THEME_WHITE : col);
        D->setTextColor(fg, bg);
        D->setTextSize(1);
        int lw = strlen(actLabels[a]) * 6;
        D->setCursor(curAx + (actW[a] - lw) / 2, kbY + 8);
        D->print(actLabels[a]);

        curAx += actW[a] + 4;
    }

    // Rows 0..3: 10-Key x 4-Row Character Matrix
    static const char* kbdMatrix[4] = {
        "1234567890",
        "QWERTYUIOP",
        "ASDFGHJKL-",
        "ZXCVBNM.,/"
    };

    int kStartY = kbY + 24;
    int kKeyW = 20, kKeyH = 16, kPad = 3;

    for (int r = 0; r < 4; r++) {
        int rStartX = kbX + 5;
        for (int c = 0; c < 10; c++) {
            int kx = rStartX + c * (kKeyW + kPad);
            int ky = kStartY + r * (kKeyH + kPad);
            bool isSel = (keyRow == r && keyCol == c);

            uint16_t kBg = isSel ? THEME_CYAN : THEME_SURFACE;
            uint16_t kFg = isSel ? THEME_BG : THEME_WHITE;
            D->fillRoundRect(kx, ky, kKeyW, kKeyH, 2, kBg);
            if (isSel) D->drawRoundRect(kx, ky, kKeyW, kKeyH, 2, THEME_WHITE);

            D->setTextColor(kFg, kBg);
            D->setCursor(kx + 7, ky + 4);
            D->print(kbdMatrix[r][c]);
        }
    }

    // 4. Exit Confirmation Dialog Overlay (SAVE vs DISCARD)
    if (exitPrompt) {
        int pw = 216, ph = 96;
        int px = (SCREEN_WIDTH - pw) / 2;
        int py = 88;

        // Dark modal backdrop with cyber border
        D->fillRoundRect(px, py, pw, ph, 5, THEME_PANEL);
        D->drawRoundRect(px, py, pw, ph, 5, THEME_AMBER);
        D->drawRoundRect(px + 1, py + 1, pw - 2, ph - 2, 4, THEME_AMBER);

        // Header warning
        D->setTextSize(1);
        D->setTextColor(THEME_AMBER, THEME_PANEL);
        D->setCursor(px + 14, py + 12);
        D->print("! UNSAVED NOTE CHANGES !");

        // Subtitle message
        D->setTextColor(THEME_WHITE, THEME_PANEL);
        D->setCursor(px + 14, py + 26);
        D->print("Save or discard before exit?");

        // Option Buttons: [SAVE & EXIT] vs [DISCARD]
        int btnW = 90, btnH = 22;
        int btnY = py + 46;

        // Button 0: SAVE & EXIT
        int b0X = px + 12;
        bool b0Sel = (promptChoice == 0);
        uint16_t b0Bg = b0Sel ? THEME_GREEN : THEME_SURFACE;
        uint16_t b0Fg = b0Sel ? THEME_BG : THEME_GREEN;
        D->fillRoundRect(b0X, btnY, btnW, btnH, 3, b0Bg);
        D->drawRoundRect(b0X, btnY, btnW, btnH, 3, b0Sel ? THEME_WHITE : THEME_GREEN);
        D->setTextColor(b0Fg, b0Bg);
        D->setCursor(b0X + 11, btnY + 7);
        D->print("SAVE & EXIT");

        // Button 1: DISCARD
        int b1X = px + pw - btnW - 12;
        bool b1Sel = (promptChoice == 1);
        uint16_t b1Bg = b1Sel ? THEME_CRIMSON : THEME_SURFACE;
        uint16_t b1Fg = b1Sel ? THEME_WHITE : THEME_CRIMSON;
        D->fillRoundRect(b1X, btnY, btnW, btnH, 3, b1Bg);
        D->drawRoundRect(b1X, btnY, btnW, btnH, 3, b1Sel ? THEME_WHITE : THEME_CRIMSON);
        D->setTextColor(b1Fg, b1Bg);
        D->setCursor(b1X + 22, btnY + 7);
        D->print("DISCARD");

        // Footer hint
        D->setTextColor(THEME_GREY, THEME_PANEL);
        D->setCursor(px + 18, py + 78);
        D->print("L/R:Select  CLICK:OK  MENU:Cancel");

        drawBottomBar("L/R:Select  CLICK:Confirm  MENU:Cancel");
        return;
    }

    drawBottomBar("JOY:Keys  CLICK:Type  SHUTTER:Save  MENU:Exit");
}




// ============================================================
// Procedural dynamic space scenery backdrop (randomized stars and celestial bodies)
static void drawSpaceScenery(int playX, int playY, int playW, int playH, int seed, uint32_t tickMs) {
    // 1. Deep cosmic backdrop
    D->fillRoundRect(playX, playY, playW, playH, 4, THEME_BG);

    // 2. Deterministic pseudo-random twinkling stars
    uint32_t rng = (seed == 0) ? 133742 : (uint32_t)seed;
    for (int i = 0; i < 34; i++) {
        rng = rng * 1664525 + 1013904223;
        int sx = playX + 4 + (rng % (playW - 8));
        rng = rng * 1664525 + 1013904223;
        int sy = playY + 22 + (rng % (playH - 26));

        bool twinkle = ((tickMs / 320 + i * 5) % 6 == 0);
        uint16_t col = (i % 3 == 0) ? THEME_CYAN : (i % 5 == 0 ? THEME_AMBER : THEME_WHITE);
        if (twinkle) {
            D->drawPixel(sx, sy, THEME_WHITE);
            if (i % 4 == 0) {
                D->drawPixel(sx - 1, sy, THEME_CYAN);
                D->drawPixel(sx + 1, sy, THEME_CYAN);
                D->drawPixel(sx, sy - 1, THEME_CYAN);
                D->drawPixel(sx, sy + 1, THEME_CYAN);
            }
        } else {
            D->drawPixel(sx, sy, (i % 2 == 0) ? col : THEME_DIM_CYAN);
        }
    }

    // 3. Randomized Celestial Scenery Feature
    int celestialType = (seed & 0x7FFF) % 4;
    if (celestialType == 0) {
        // Ringed Gas Giant Planet (Upper Right)
        int cx = playX + playW - 46, cy = playY + 54, r = 14;
        D->fillCircle(cx, cy, r, THEME_PANEL);
        D->drawCircle(cx, cy, r, THEME_CYAN);
        D->drawCircle(cx + 1, cy, r - 3, THEME_DIM_CYAN);
        // Rings passing through
        for (int rx = -24; rx <= 24; rx += 2) {
            int ry = cy + rx / 3;
            if (abs(rx) > r - 2 || ry > cy) {
                D->drawPixel(cx + rx, ry, THEME_AMBER);
                D->drawPixel(cx + rx, ry + 1, THEME_DIM_CYAN);
            }
        }
    } else if (celestialType == 1) {
        // Glowing Cyber Moon with soft craters (Upper Left)
        int cx = playX + 46, cy = playY + 58, r = 16;
        D->fillCircle(cx, cy, r, THEME_PANEL);
        D->drawCircle(cx, cy, r, THEME_WHITE);
        D->drawCircle(cx - 5, cy - 3, 3, THEME_DIM_CYAN);
        D->drawCircle(cx + 4, cy + 4, 4, THEME_DIM_CYAN);
        D->drawCircle(cx - 2, cy + 7, 2, THEME_DIM_CYAN);
    } else if (celestialType == 2) {
        // Distant Binary Sun System (Upper Center)
        int cx1 = playX + playW / 2 - 20, cy1 = playY + 46;
        int cx2 = cx1 + 26, cy2 = cy1 - 8;
        D->fillCircle(cx1, cy1, 9, THEME_AMBER);
        D->drawCircle(cx1, cy1, 10, THEME_WHITE);
        D->fillCircle(cx2, cy2, 6, THEME_CYAN);
        D->drawCircle(cx2, cy2, 7, THEME_WHITE);
    } else {
        // Cosmic Nebula Cloud formation
        int nx = playX + playW / 2, ny = playY + 52;
        D->fillCircle(nx - 22, ny + 4, 12, THEME_PANEL);
        D->fillCircle(nx, ny - 6, 16, THEME_PANEL);
        D->fillCircle(nx + 20, ny + 2, 11, THEME_PANEL);
        D->drawCircle(nx, ny - 6, 17, THEME_DIM_CYAN);
        D->drawCircle(nx - 22, ny + 4, 13, THEME_DIM_CYAN);
    }

    // Outer cyber border
    D->drawRoundRect(playX, playY, playW, playH, 4, THEME_CYAN);
}

// ============================================================
// RETRO MINI-GAMES RENDERER (buffered)
// ============================================================
void DisplayUI::drawRetroGames(int gameId, int subState, int score, int highScore,
                               int p1X, int p1Y, int p2X, int p2Y, int extra1, int extra2,
                               const void* extraData, int spaceSeed) {
    // Top Bar
    const char* titles[] = {
        "CYBER ARCADE // SELECT",
        "GAME // CYBER SNAKE",
        "GAME // NEON PONG",
        "GAME // CYBER FLAPPY",
        "GAME // NEON BREAKOUT"
    };
    drawTopBar(titles[gameId < 5 ? gameId : 0], THEME_GREEN);

    int playX = 6, playY = 24, playW = SCREEN_WIDTH - 12, playH = SCREEN_HEIGHT - 46;

    if (gameId == 0) {
        D->fillRoundRect(playX, playY, playW, playH, 4, THEME_SURFACE);
        D->drawRoundRect(playX, playY, playW, playH, 4, THEME_CYAN);

        // MENU SELECT (4 games, balanced clean cards)
        drawCenteredText("SELECT MINI-GAME", playY + 14, THEME_GREEN, 1);

        const char* gNames[] = {
            "1. CYBER SNAKE",
            "2. NEON PONG",
            "3. CYBER FLAPPY",
            "4. NEON BREAKOUT"
        };
        const char* gDescs[] = {
            "Retro crawler // Space grid",
            "Neon duel // AI paddle match",
            "Thrust flight // Cosmic conduits",
            "Arcade brick smash // Rebound"
        };

        for (int i = 0; i < 4; i++) {
            int gy = playY + 36 + i * 48;
            bool sel = (subState == i);
            uint16_t bg = sel ? THEME_PANEL : THEME_SURFACE;
            uint16_t border = sel ? THEME_GREEN : THEME_DIM_CYAN;

            D->fillRoundRect(playX + 6, gy, playW - 12, 42, 4, bg);
            D->drawRoundRect(playX + 6, gy, playW - 12, 42, 4, border);

            if (sel) {
                D->fillRoundRect(playX + 6, gy + 2, 4, 38, 2, THEME_GREEN);
                D->setTextColor(THEME_GREEN, bg);
                D->setTextSize(1);
                D->setCursor(playX + 14, gy + 8);
                D->print("> ");
            }

            D->setTextColor(sel ? THEME_WHITE : THEME_CYAN, bg);
            D->setTextSize(1);
            D->setCursor(playX + 26, gy + 8);
            D->print(gNames[i]);

            D->setTextColor(THEME_GREY, bg);
            D->setCursor(playX + 26, gy + 24);
            D->print(gDescs[i]);
        }
        drawBottomBar("JOY:Select  CLICK:Play  MENU:Exit");
        return;
    }

    // GAMEPLAY: Draw dynamic randomized space scenery background
    drawSpaceScenery(playX, playY, playW, playH, spaceSeed, millis());

    // Header stats
    char statBuf[40];
    snprintf(statBuf, sizeof(statBuf), "SCORE:%04d  HIGH:%04d", score, highScore);
    D->setTextColor(THEME_GREEN, THEME_BG);
    D->setTextSize(1);
    D->setCursor(playX + 10, playY + 6);
    D->print(statBuf);
    D->drawFastHLine(playX + 2, playY + 18, playW - 4, THEME_DIM_CYAN);

    if (gameId == 1) {
        // CYBER SNAKE (Rendered over space starfield)
        const uint16_t* body = (const uint16_t*)extraData;
        int len = extra1;
        int foodX = p2X, foodY = p2Y;

        // Glowing pulse food
        D->fillCircle(playX + 6 + foodX * 8 + 4, playY + 24 + foodY * 8 + 4, 3, THEME_CRIMSON);
        D->drawCircle(playX + 6 + foodX * 8 + 4, playY + 24 + foodY * 8 + 4, 4, THEME_WHITE);

        // Snake Body
        if (body && len > 0) {
            for (int i = 0; i < len; i++) {
                int bx = body[i] & 0xFF;
                int by = (body[i] >> 8) & 0xFF;
                uint16_t col = (i == 0) ? THEME_WHITE : (i % 2 == 0 ? THEME_GREEN : THEME_CYAN);
                D->fillRect(playX + 6 + bx * 8 + 1, playY + 24 + by * 8 + 1, 6, 6, col);
            }
        }
    } else if (gameId == 2) {
        // NEON PONG (Rendered over space scenery)
        int padW = 4, padH = 34;
        // Player Paddle (Left)
        D->fillRoundRect(playX + 8, p1Y, padW, padH, 2, THEME_CYAN);
        // AI Paddle (Right)
        D->fillRoundRect(playX + playW - 12, p2Y, padW, padH, 2, THEME_CRIMSON);
        // Center Net
        for (int ny = playY + 22; ny < playY + playH - 10; ny += 10) {
            D->drawFastVLine(playX + playW / 2, ny, 5, THEME_DIM_CYAN);
        }
        // Ball
        D->fillCircle(extra1, extra2, 3, THEME_WHITE);
        D->drawCircle(extra1, extra2, 4, THEME_GREEN);
    } else if (gameId == 3) {
        // CYBER FLAPPY (Drone navigating space conduits)
        int birdX = 50, birdY = p1Y;
        int pipeX = extra1, pipeGapY = extra2;
        int pipeW = 20, gapH = 68;

        // Cyber Drone with thruster spark
        D->fillCircle(birdX, birdY, 5, THEME_AMBER);
        D->drawCircle(birdX, birdY, 6, THEME_WHITE);
        D->drawFastHLine(birdX - 7, birdY, 5, THEME_CYAN);

        // Space Conduit Pipes (Top & Bottom)
        if (pipeX + pipeW > playX && pipeX < playX + playW) {
            int topH = pipeGapY - (playY + 20);
            if (topH > 0) {
                D->fillRect(pipeX, playY + 20, pipeW, topH, THEME_PANEL);
                D->drawRect(pipeX, playY + 20, pipeW, topH, THEME_GREEN);
                D->fillRect(pipeX - 2, playY + 20 + topH - 6, pipeW + 4, 6, THEME_GREEN);
            }
            int botY = pipeGapY + gapH;
            int botH = (playY + playH) - botY;
            if (botH > 0) {
                D->fillRect(pipeX, botY, pipeW, botH, THEME_PANEL);
                D->drawRect(pipeX, botY, pipeW, botH, THEME_GREEN);
                D->fillRect(pipeX - 2, botY, pipeW + 4, 6, THEME_GREEN);
            }
        }
    } else if (gameId == 4) {
        // NEON BREAKOUT (Space brick breaker)
        int padX = p1X, padY = playY + playH - 12;
        int padW = 38, padH = 5;

        // Paddle
        D->fillRoundRect(padX, padY, padW, padH, 2, THEME_CYAN);
        D->drawRoundRect(padX, padY, padW, padH, 2, THEME_WHITE);

        // Ball
        D->fillCircle(extra1, extra2, 3, THEME_WHITE);
        D->drawCircle(extra1, extra2, 4, THEME_AMBER);

        // Bricks matrix (3 rows x 8 cols)
        const uint8_t* bricks = (const uint8_t*)extraData;
        if (bricks) {
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 8; c++) {
                    int bidx = r * 8 + c;
                    if (bricks[bidx]) {
                        int bx = playX + 12 + c * 26;
                        int by = playY + 28 + r * 14;
                        uint16_t bCol = (r == 0) ? THEME_CRIMSON : (r == 1 ? THEME_AMBER : THEME_GREEN);
                        D->fillRect(bx, by, 22, 10, bCol);
                        D->drawRect(bx, by, 22, 10, THEME_WHITE);
                    }
                }
            }
        }
    }

    if (subState == 2) { // GAME OVER
        int goW = 160, goH = 50;
        int goX = (SCREEN_WIDTH - goW) / 2;
        int goY = (SCREEN_HEIGHT - goH) / 2;
        D->fillRoundRect(goX, goY, goW, goH, 5, THEME_PANEL);
        D->drawRoundRect(goX, goY, goW, goH, 5, THEME_CRIMSON);
        D->setTextColor(THEME_CRIMSON, THEME_PANEL);
        D->setTextSize(1);
        D->setCursor(goX + 42, goY + 12);
        D->print("GAME OVER");
        D->setTextColor(THEME_WHITE, THEME_PANEL);
        D->setCursor(goX + 22, goY + 30);
        D->print("CLICK / SNAP: Restart");
    }

    drawBottomBar("JOY:Controls  CLICK:Action  MENU:Exit");
}

// ============================================================
// 1. TACTICAL DRONE REMOTE-ID RADAR DISPLAY
// ============================================================
void DisplayUI::drawTacticalDroneRadar(uint8_t droneCount, int selectedIdx, float radarAngle) {
    drawTopBar("AIRSPACE // DRONE REMOTE-ID", THEME_CYAN);

    // Plan Position Indicator (PPI) Circular Radar Scope
    int rx = 64, ry = 86, rRad = 52;
    D->fillCircle(rx, ry, rRad, THEME_PANEL);
    D->drawCircle(rx, ry, rRad, THEME_DIM_CYAN);
    D->drawCircle(rx, ry, rRad * 2 / 3, 0x0842);
    D->drawCircle(rx, ry, rRad / 3, 0x0842);

    // Range rings & crosshairs
    D->drawFastHLine(rx - rRad, ry, rRad * 2, 0x0842);
    D->drawFastVLine(rx, ry - rRad, rRad * 2, 0x0842);
    D->setTextColor(0x0842, THEME_PANEL);
    D->setTextSize(1);
    D->setCursor(rx + 2, ry - rRad + 3); D->print("N");
    D->setCursor(rx + rRad - 8, ry + 2); D->print("E");

    // Rotating Radar Sweep Beam Line
    float rad = radarAngle * 0.0174533f;
    int sweepX = rx + (int)(cosf(rad) * (rRad - 2));
    int sweepY = ry + (int)(sinf(rad) * (rRad - 2));
    D->drawLine(rx, ry, sweepX, sweepY, THEME_GREEN);

    // Render drone target blips on radar scope
    for (uint8_t i = 0; i < droneCount; i++) {
        DroneTarget drone;
        if (TacticalSuite::getDrone(i, drone)) {
            // Plot relative distance and bearing
            float distNorm = fminf(1.0f, (float)(abs(drone.rssi) - 40) / 55.0f);
            float bRad = (float)drone.headingDeg * 0.0174533f;
            int bx = rx + (int)(sinf(bRad) * (rRad - 8) * (1.0f - distNorm * 0.7f));
            int by = ry - (int)(cosf(bRad) * (rRad - 8) * (1.0f - distNorm * 0.7f));

            bool isSel = (i == selectedIdx);
            uint16_t bCol = isSel ? THEME_WHITE : (drone.uaType == 2 ? THEME_CYAN : THEME_AMBER);
            D->fillCircle(bx, by, isSel ? 3 : 2, bCol);
            if (isSel) D->drawCircle(bx, by, 5, THEME_GREEN);
        }
    }

    // Right-side Selected Target Telemetry Card (x = 124 to 236)
    int cx = 124, cy = 24, cw = 112, ch = 124;
    D->fillRoundRect(cx, cy, cw, ch, 3, THEME_PANEL);
    D->drawRoundRect(cx, cy, cw, ch, 3, THEME_CYAN);

    DroneTarget selDrone;
    bool hasSel = TacticalSuite::getDrone(selectedIdx, selDrone);

    D->setTextSize(1);
    D->setTextColor(THEME_CYAN, THEME_PANEL);
    D->setCursor(cx + 4, cy + 4);
    D->print("TARGET TELEMETRY");
    D->drawFastHLine(cx + 2, cy + 13, cw - 4, THEME_DIM_CYAN);

    if (hasSel) {
        D->setTextColor(THEME_WHITE, THEME_PANEL);
        D->setCursor(cx + 4, cy + 16);
        D->printf("ID: %s", selDrone.uasId);

        D->setTextColor(THEME_GREY, THEME_PANEL);
        D->setCursor(cx + 4, cy + 28);
        D->printf("TYPE: %s", selDrone.uaType == 2 ? "MULTIROTOR" : (selDrone.uaType == 1 ? "PLANE" : "UAV"));

        D->setCursor(cx + 4, cy + 40);
        D->printf("ALT: %.1fm", selDrone.altitudeGeoM);

        D->setCursor(cx + 4, cy + 52);
        D->printf("SPD: %.1fm/s", selDrone.speedMps);

        D->setCursor(cx + 4, cy + 64);
        D->printf("HDG: %d DEG", selDrone.headingDeg);

        D->setCursor(cx + 4, cy + 76);
        D->printf("CH: %d  SIG:%ddB", selDrone.channel, selDrone.rssi);

        // Pilot Link status
        D->setCursor(cx + 4, cy + 88);
        if (selDrone.hasPilot) {
            D->setTextColor(THEME_GREEN, THEME_PANEL);
            D->print("PILOT: GPS ACQ");
        } else {
            D->setTextColor(THEME_AMBER, THEME_PANEL);
            D->print("PILOT: NO FIX");
        }

        // Threat level badge
        D->fillRoundRect(cx + 4, cy + 102, cw - 8, 16, 2, selDrone.rssi > -65 ? THEME_CRIMSON : THEME_CYAN);
        D->setTextColor(THEME_BG, selDrone.rssi > -65 ? THEME_CRIMSON : THEME_CYAN);
        D->setCursor(cx + 8, cy + 106);
        D->print(selDrone.rssi > -65 ? "! CLOSE PROX !" : "AIRSPACE TRACK");
    } else {
        D->setTextColor(THEME_GREY, THEME_PANEL);
        D->setCursor(cx + 6, cy + 40);
        D->print("SCANNING 2.4G...");
        D->setCursor(cx + 6, cy + 54);
        D->print("OPENDRONEID F3411");
    }

    // Bottom Discovered Airspace Targets Log Table (y = 152 to 296)
    int tblY = 152;
    D->fillRoundRect(4, tblY, SCREEN_WIDTH - 8, 144, 3, THEME_PANEL);
    D->drawRoundRect(4, tblY, SCREEN_WIDTH - 8, 144, 3, THEME_DIM_CYAN);

    D->setTextColor(THEME_CYAN, THEME_PANEL);
    D->setCursor(8, tblY + 4);
    D->printf("AIRSPACE CONTACTS [%d ACTIVE]", droneCount);
    D->drawFastHLine(6, tblY + 14, SCREEN_WIDTH - 12, THEME_DIM_CYAN);

    int rowY = tblY + 18;
    for (uint8_t i = 0; i < droneCount && i < 6; i++) {
        DroneTarget d;
        if (TacticalSuite::getDrone(i, d)) {
            bool sel = (i == selectedIdx);
            if (sel) {
                D->fillRect(6, rowY - 1, SCREEN_WIDTH - 12, 19, THEME_SURFACE);
                D->drawRect(6, rowY - 1, SCREEN_WIDTH - 12, 19, THEME_CYAN);
            }
            D->setTextColor(sel ? THEME_WHITE : THEME_GREY, sel ? THEME_SURFACE : THEME_PANEL);
            D->setCursor(10, rowY + 3);
            D->printf("%-15s", d.uasId);

            D->setCursor(115, rowY + 3);
            D->printf("%3ddB", d.rssi);

            D->setCursor(160, rowY + 3);
            D->printf("%.0fm", d.altitudeGeoM);

            D->setTextColor(d.rssi > -65 ? THEME_CRIMSON : THEME_GREEN, sel ? THEME_SURFACE : THEME_PANEL);
            D->setCursor(200, rowY + 3);
            D->print(d.rssi > -65 ? "WARN" : "OK");

            rowY += 20;
        }
    }

    drawBottomBar("JOY:Select  CLICK:Lock Pilot  SHUTTER:Log  MENU:Exit");
}

// ============================================================
// 2. TACTICAL SIGNAL FOX-HUNT PROXIMITY RADAR
// ============================================================
void DisplayUI::drawTacticalFoxHunt(const char* name, const uint8_t mac[6], int8_t rssi, float filteredRssi,
                                    int8_t peakRssi, float estDistanceM, uint8_t channel, bool isLocked, float radarAngle) {
    drawTopBar("FOX-HUNT // SIGNAL PROXIMITY", THEME_AMBER);

    // Target Locked Header Banner
    int hX = 4, hY = 22, hW = SCREEN_WIDTH - 8, hH = 34;
    D->fillRoundRect(hX, hY, hW, hH, 3, THEME_PANEL);
    D->drawRoundRect(hX, hY, hW, hH, 3, isLocked ? THEME_AMBER : THEME_GREY);

    D->setTextSize(1);
    D->setTextColor(isLocked ? THEME_AMBER : THEME_GREY, THEME_PANEL);
    D->setCursor(hX + 6, hY + 4);
    D->printf("LOCKED TARGET: %s", name ? name : "NONE");

    char macStr[20];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    D->setTextColor(THEME_WHITE, THEME_PANEL);
    D->setCursor(hX + 6, hY + 18);
    D->printf("MAC: %s  CH:%d", macStr, channel);

    // Massive Cyber RSSI Gauges & Heat Bar
    int gY = 60;
    D->fillRoundRect(hX, gY, hW, 90, 4, THEME_PANEL);
    D->drawRoundRect(hX, gY, hW, 90, 4, THEME_DIM_CYAN);

    // Giant RSSI Readout
    D->setTextSize(2);
    uint16_t sigCol = filteredRssi > -60.0f ? THEME_CRIMSON : (filteredRssi > -75.0f ? THEME_AMBER : THEME_GREEN);
    D->setTextColor(sigCol, THEME_PANEL);
    D->setCursor(hX + 10, gY + 10);
    D->printf("%ddBm", (int)filteredRssi);

    // Distance & Peak readings
    D->setTextSize(1);
    D->setTextColor(THEME_WHITE, THEME_PANEL);
    D->setCursor(hX + 115, gY + 10);
    D->printf("EST DIST: ~%.1fm", estDistanceM);

    D->setTextColor(THEME_GREY, THEME_PANEL);
    D->setCursor(hX + 115, gY + 22);
    D->printf("PEAK SIG: %ddBm", peakRssi);

    // Segmented Multi-Bar LED Proximity Meter
    int barY = gY + 42;
    int segW = 12, segH = 22, segPad = 2, totalSegs = 16;
    float normSig = fmaxf(0.0f, fminf(1.0f, (filteredRssi + 95.0f) / 65.0f));
    int litSegs = (int)(normSig * (float)totalSegs);

    for (int s = 0; s < totalSegs; s++) {
        int sx = hX + 8 + s * (segW + segPad);
        bool lit = (s < litSegs);
        uint16_t segCol = s < 6 ? THEME_GREEN : (s < 12 ? THEME_AMBER : THEME_CRIMSON);
        D->fillRect(sx, barY, segW, segH, lit ? segCol : 0x0842);
        D->drawRect(sx, barY, segW, segH, lit ? THEME_WHITE : 0x1084);
    }

    D->setTextColor(THEME_GREY, THEME_PANEL);
    D->setCursor(hX + 8, barY + 28);
    D->print("-95dBm [FAR]                       -30dBm [CLOSE]");

    // Directional RF Sampling Sweep Scope (y = 154 to 296)
    int sY = 154, sH = 142;
    D->fillRoundRect(hX, sY, hW, sH, 4, THEME_PANEL);
    D->drawRoundRect(hX, sY, hW, sH, 4, THEME_AMBER);

    // Mini circular radar
    int mrX = 64, mrY = sY + 70, mrRad = 48;
    D->fillCircle(mrX, mrY, mrRad, THEME_SURFACE);
    D->drawCircle(mrX, mrY, mrRad, THEME_AMBER);
    D->drawCircle(mrX, mrY, mrRad / 2, 0x18C4);

    float sRad = radarAngle * 0.0174533f;
    int spX = mrX + (int)(cosf(sRad) * (mrRad - 2));
    int spY = mrY + (int)(sinf(sRad) * (mrRad - 2));
    D->drawLine(mrX, mrY, spX, spY, THEME_AMBER);

    // Target Proximity Arc
    int tArcR = (int)(mrRad * (1.0f - normSig * 0.75f));
    D->drawCircle(mrX, mrY, fmax(4, tArcR), sigCol);

    // Right-side Geiger & Hunt Guide
    D->setTextColor(THEME_AMBER, THEME_PANEL);
    D->setCursor(124, sY + 16);
    D->print("GEIGER PULSER");

    D->setTextColor(THEME_WHITE, THEME_PANEL);
    D->setCursor(124, sY + 32);
    int pHz = (int)(2 + normSig * 24);
    D->printf("RATE: %d Hz", pHz);

    D->setTextColor(THEME_GREY, THEME_PANEL);
    D->setCursor(124, sY + 50);
    D->print("ROTATE DEVICE");
    D->setCursor(124, sY + 62);
    D->print("TO LOCATE APEX");
    D->setCursor(124, sY + 74);
    D->print("OF MAXIMUM RF");

    // Audio status box
    D->fillRoundRect(124, sY + 92, 100, 24, 3, THEME_SURFACE);
    D->drawRoundRect(124, sY + 92, 100, 24, 3, THEME_GREEN);
    D->setTextColor(THEME_GREEN, THEME_SURFACE);
    D->setCursor(130, sY + 100);
    D->print("BUZZER ACTIVE");

    drawBottomBar("JOY:Target  CLICK:Reset Peak  SHUTTER:Log  MENU:Exit");
}

// ============================================================
// 3. COVERT SKIMMER & SURVEILLANCE RADAR SCANNER
// ============================================================
void DisplayUI::drawTacticalSkimmerScanner(uint8_t count, int selectedIdx, uint32_t scanMs) {
    drawTopBar("SURVEILLANCE // SKIMMER RADAR", THEME_CRIMSON);

    // Threat Status Bar Ribbon
    int sX = 4, sY = 22, sW = SCREEN_WIDTH - 8;
    D->fillRoundRect(sX, sY, sW, 26, 3, THEME_PANEL);
    D->drawRoundRect(sX, sY, sW, 26, 3, THEME_CRIMSON);

    // Pulse dot
    if ((millis() / 400) % 2 == 0) {
        D->fillCircle(sX + 10, sY + 13, 4, THEME_CRIMSON);
    }
    D->setTextSize(1);
    D->setTextColor(THEME_WHITE, THEME_PANEL);
    D->setCursor(sX + 20, sY + 9);
    D->printf("MONITORING: %d TARGETS DETECTED", count);

    // Discovered Surveillance Nodes List
    int listY = 52, listH = 244;
    D->fillRoundRect(sX, listY, sW, listH, 4, THEME_PANEL);
    D->drawRoundRect(sX, listY, sW, listH, 4, THEME_DIM_CYAN);

    int rowY = listY + 6;
    for (uint8_t i = 0; i < count && i < 6; i++) {
        SurveillanceNode node;
        if (TacticalSuite::getSurveillanceNode(i, node)) {
            bool sel = (i == selectedIdx);
            int rh = 36;
            D->fillRoundRect(sX + 4, rowY, sW - 8, rh, 3, sel ? THEME_SURFACE : THEME_PANEL);
            D->drawRoundRect(sX + 4, rowY, sW - 8, rh, 3, sel ? THEME_WHITE : (node.threat == THREAT_CRITICAL ? THEME_CRIMSON : THEME_AMBER));

            // Type Badge
            const char* typeTag = "UNKNOWN";
            uint16_t tagBg = THEME_GREY;
            if (node.type == SURV_SKIMMER_HC05 || node.type == SURV_SKIMMER_RN42) {
                typeTag = "SKIMMER"; tagBg = THEME_CRIMSON;
            } else if (node.type == SURV_FLOCK_ALPR) {
                typeTag = "ALPR CAM"; tagBg = THEME_CRIMSON;
            } else if (node.type == SURV_AIRTAG_STALKER) {
                typeTag = "AIRTAG"; tagBg = THEME_AMBER;
            } else if (node.type == SURV_FLIPPER_ZERO) {
                typeTag = "FLIPPER"; tagBg = THEME_CYAN;
            } else if (node.type == SURV_PWNAGOTCHI) {
                typeTag = "PWNAGOTCHI"; tagBg = THEME_GREEN;
            }

            D->fillRoundRect(sX + 8, rowY + 5, 56, 12, 2, tagBg);
            D->setTextColor(THEME_BG, tagBg);
            D->setCursor(sX + 11, rowY + 7);
            D->print(typeTag);

            // Signal bar
            D->setTextColor(THEME_WHITE, sel ? THEME_SURFACE : THEME_PANEL);
            D->setCursor(sX + 70, rowY + 7);
            D->printf("%ddBm", node.rssi);

            // Name
            D->setTextColor(sel ? THEME_WHITE : THEME_CYAN, sel ? THEME_SURFACE : THEME_PANEL);
            D->setCursor(sX + 8, rowY + 21);
            D->printf("%-24s", node.name);

            // MAC
            char mBuf[18];
            snprintf(mBuf, sizeof(mBuf), "%02X:%02X:%02X", node.mac[3], node.mac[4], node.mac[5]);
            D->setTextColor(THEME_GREY, sel ? THEME_SURFACE : THEME_PANEL);
            D->setCursor(sX + sW - 68, rowY + 7);
            D->print(mBuf);

            rowY += rh + 4;
        }
    }

    drawBottomBar("JOY:Scroll  CLICK:Fox-Hunt Lock  MENU:Exit");
}

LGFX_Sprite* DisplayUI::createPsramSprite(int w, int h) {
    LGFX_Sprite* sprite = new LGFX_Sprite(&tft);
    sprite->setPsram(true);
    sprite->setColorDepth(16);
    if (!sprite->createSprite(w, h)) {
        delete sprite;
        return nullptr;
    }
    return sprite;
}
