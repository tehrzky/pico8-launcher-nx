#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// =============================================================================
// Config
// =============================================================================
struct Config {
    std::string rom_path = "sdmc:/switch/pico8-launcher/carts/";
    std::string fake08_path = "sdmc:/switch/pico8-launcher/FAKE-08.nro";
    std::string launcher_path = "sdmc:/switch/pico8-launcher/pico8-launcher.nro";
};

struct RomEntry {
    std::string filename;
    std::string fullPath;
    SDL_Texture* texture = nullptr;
    int origW = 0;
    int origH = 0;
};

enum AppState {
    STATE_GRID,
    STATE_SETTINGS
};

Config g_config;
const std::string CONFIG_FILE = "sdmc:/switch/pico8-launcher/config.txt";

// =============================================================================
// Layout: 6 columns × 2 rows = 12 games
// =============================================================================
constexpr int SCREEN_W = 1280;
constexpr int SCREEN_H = 720;
constexpr int HEADER_H = 50;
constexpr int FOOTER_H = 45;

constexpr int COLS = 6;
constexpr int ROWS = 2;
constexpr int ITEMS_PER_PAGE = COLS * ROWS; // 12

constexpr int CARD_W = 195;
constexpr int CARD_H = 280;
constexpr int GAP_X = 12;
constexpr int GAP_Y = 15;
constexpr int START_X = 20;
constexpr int GRID_Y = 60;

constexpr int THUMB_MAX_W = 175;
constexpr int THUMB_MAX_H = 220;

// =============================================================================
// Colors
// =============================================================================
const SDL_Color C_BG       = { 16, 16, 22, 255 };
const SDL_Color C_PANEL    = { 30, 30, 40, 255 };
const SDL_Color C_HEADER   = { 25, 25, 35, 255 };
const SDL_Color C_FOOTER   = { 22, 22, 30, 255 };
const SDL_Color C_BORDER   = { 55, 55, 70, 255 };
const SDL_Color C_ACCENT   = { 0, 200, 140, 255 };
const SDL_Color C_SELECTED = { 0, 150, 220, 255 };
const SDL_Color C_WHITE    = { 255, 255, 255, 255 };
const SDL_Color C_MUTED    = { 140, 140, 160, 255 };
const SDL_Color C_ERROR    = { 220, 60, 60, 255 };
const SDL_Color C_OVERLAY  = { 10, 10, 16, 240 };

// =============================================================================
// Config I/O
// =============================================================================
void loadConfig() {
    std::ifstream file(CONFIG_FILE);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        size_t delim = line.find('=');
        if (delim == std::string::npos) continue;
        std::string key = line.substr(0, delim);
        std::string val = line.substr(delim + 1);
        if (key == "rom_path") g_config.rom_path = val;
        else if (key == "fake08_path") g_config.fake08_path = val;
        else if (key == "launcher_path") g_config.launcher_path = val;
    }
}

void saveConfig() {
    fs::create_directories("sdmc:/switch/pico8-launcher/");
    std::ofstream file(CONFIG_FILE);
    if (!file.is_open()) return;
    file << "rom_path=" << g_config.rom_path << "\n";
    file << "fake08_path=" << g_config.fake08_path << "\n";
    file << "launcher_path=" << g_config.launcher_path << "\n";
}

// =============================================================================
// ROM Scanner
// =============================================================================
std::vector<RomEntry> scanRoms(const std::string& path) {
    std::vector<RomEntry> roms;
    if (!fs::exists(path)) return roms;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".p8" || ext == ".png") {
                roms.push_back({ entry.path().filename().string(), entry.path().string(), nullptr, 0, 0 });
            }
        }
    }
    std::sort(roms.begin(), roms.end(), [](const RomEntry& a, const RomEntry& b) {
        return a.filename < b.filename;
    });
    return roms;
}

// =============================================================================
// Texture Loader (keeps original aspect ratio)
// =============================================================================
void loadPageTextures(SDL_Renderer* renderer, std::vector<RomEntry>& roms, int pageStart, int count) {
    for (size_t i = 0; i < roms.size(); ++i) {
        bool inRange = (static_cast<int>(i) >= pageStart && static_cast<int>(i) < pageStart + count);
        if (inRange && roms[i].texture == nullptr) {
            SDL_Surface* surf = IMG_Load(roms[i].fullPath.c_str());
            if (!surf) {
                std::string pngAlt = roms[i].fullPath + ".png";
                surf = IMG_Load(pngAlt.c_str());
            }
            if (surf) {
                roms[i].texture = SDL_CreateTextureFromSurface(renderer, surf);
                roms[i].origW = surf->w;
                roms[i].origH = surf->h;
                SDL_FreeSurface(surf);
            }
        } else if (!inRange && roms[i].texture != nullptr) {
            SDL_DestroyTexture(roms[i].texture);
            roms[i].texture = nullptr;
            roms[i].origW = 0;
            roms[i].origH = 0;
        }
    }
}

// =============================================================================
// ORIGINAL WORKING launchFake08
// =============================================================================
void launchFake08(const std::string& fake08Path, const std::string& romFullPath) {
    char argBuffer[1024];
    snprintf(argBuffer, sizeof(argBuffer), "\"%s\" \"%s\"", fake08Path.c_str(), romFullPath.c_str());
    envSetNextLoad(fake08Path.c_str(), argBuffer);
}

// =============================================================================
// Helpers
// =============================================================================
std::string swkbdInput(const std::string& guide, const std::string& initial) {
    SwkbdConfig kbd;
    char out[512] = {0};
    if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetGuideText(&kbd, guide.c_str());
        swkbdConfigSetInitialText(&kbd, initial.c_str());
        swkbdShow(&kbd, out, sizeof(out));
        swkbdClose(&kbd);
    }
    return std::string(out);
}

void drawText(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, bool centerX = false) {
    if (!font || text.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    int w = surf->w, h = surf->h;
    int dx = centerX ? x - w / 2 : x;
    SDL_Rect dst = { dx, y, w, h };
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

void drawRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c, bool fill = true) {
    SDL_Rect rect = { x, y, w, h };
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    if (fill) SDL_RenderFillRect(r, &rect);
    else SDL_RenderDrawRect(r, &rect);
}

// =============================================================================
// Aspect-ratio thumbnail rect calculator
// =============================================================================
SDL_Rect getThumbRect(int cx, int cy, int origW, int origH) {
    if (origW <= 0 || origH <= 0) {
        return { cx + (CARD_W - THUMB_MAX_W) / 2, cy + 10, THUMB_MAX_W, THUMB_MAX_H };
    }
    float scale = std::min((float)THUMB_MAX_W / origW, (float)THUMB_MAX_H / origH);
    int dw = (int)(origW * scale);
    int dh = (int)(origH * scale);
    int dx = cx + (CARD_W - dw) / 2;
    int dy = cy + 10 + (THUMB_MAX_H - dh) / 2;
    return { dx, dy, dw, dh };
}

// =============================================================================
// Main
// =============================================================================
int main(int argc, char* argv[]) {
    romfsInit();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);

    SDL_Window* window = SDL_CreateWindow("PICO-8 Launcher",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    TTF_Font* fontHeader = TTF_OpenFont("romfs:/PTSans-Bold.ttf", 24);
    TTF_Font* fontBody   = TTF_OpenFont("romfs:/PTSans-Regular.ttf", 16);
    TTF_Font* fontSmall  = TTF_OpenFont("romfs:/PTSans-Regular.ttf", 14);
    if (!fontHeader) fontHeader = TTF_OpenFont("romfs:/PTSans-Regular.ttf", 24);
    if (!fontBody)   fontBody   = TTF_OpenFont("romfs:/PTSans-Regular.ttf", 16);
    if (!fontSmall)  fontSmall  = TTF_OpenFont("romfs:/PTSans-Regular.ttf", 14);

    loadConfig();
    std::vector<RomEntry> romList = scanRoms(g_config.rom_path);

    AppState state = STATE_GRID;
    int selectedIndex = 0;
    int settingsOption = 0;
    int lastPage = -1;
    bool running = true;
    int analogDelay = 0;
    #define ANALOG_DEADZONE 15000

    while (running && appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        HidAnalogStickState analog = padGetStickPos(&pad, 0);

        int totalItems = static_cast<int>(romList.size());
        int totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        if (totalPages < 1) totalPages = 1;
        int currentPage = selectedIndex / ITEMS_PER_PAGE;
        int pageStart = currentPage * ITEMS_PER_PAGE;

        if (currentPage != lastPage && state == STATE_GRID) {
            loadPageTextures(renderer, romList, pageStart, ITEMS_PER_PAGE);
            lastPage = currentPage;
        }

        // --- Analog ---
        bool analogUp    = analog.y < -ANALOG_DEADZONE;
        bool analogDown  = analog.y >  ANALOG_DEADZONE;
        bool analogLeft  = analog.x < -ANALOG_DEADZONE;
        bool analogRight = analog.x >  ANALOG_DEADZONE;
        bool analogMoved = analogUp || analogDown || analogLeft || analogRight;

        if (analogMoved) {
            if (analogDelay == 0) {
                if (state == STATE_GRID) {
                    if (analogUp)    { if (selectedIndex >= COLS) selectedIndex -= COLS; }
                    if (analogDown)  { if (selectedIndex + COLS < totalItems) selectedIndex += COLS; }
                    if (analogLeft)  { if (selectedIndex % COLS > 0) selectedIndex -= 1; }
                    if (analogRight) { if (selectedIndex % COLS < COLS - 1 && selectedIndex + 1 < totalItems) selectedIndex += 1; }
                } else {
                    if (analogUp)   settingsOption = (settingsOption - 1 + 3) % 3;
                    if (analogDown) settingsOption = (settingsOption + 1) % 3;
                }
                analogDelay = 10;
            } else {
                analogDelay--;
            }
        } else {
            analogDelay = 0;
        }

        // --- Buttons (libnx = physical Switch buttons) ---
        if (state == STATE_GRID) {
            if (kDown & HidNpadButton_Up)    { if (selectedIndex >= COLS) selectedIndex -= COLS; }
            if (kDown & HidNpadButton_Down)  { if (selectedIndex + COLS < totalItems) selectedIndex += COLS; }
            if (kDown & HidNpadButton_Left)  { if (selectedIndex % COLS > 0) selectedIndex -= 1; }
            if (kDown & HidNpadButton_Right) { if (selectedIndex % COLS < COLS - 1 && selectedIndex + 1 < totalItems) selectedIndex += 1; }

            if (kDown & HidNpadButton_L) {
                if (currentPage > 0) { selectedIndex = (currentPage - 1) * ITEMS_PER_PAGE; lastPage = -1; }
            }
            if (kDown & HidNpadButton_R) {
                if (currentPage + 1 < totalPages) { selectedIndex = (currentPage + 1) * ITEMS_PER_PAGE; lastPage = -1; }
            }

            if (kDown & HidNpadButton_A) {
                if (totalItems > 0 && selectedIndex < totalItems) {
                    launchFake08(g_config.fake08_path, romList[selectedIndex].fullPath);
                    running = false;
                }
            }
            if (kDown & HidNpadButton_X) {
                state = STATE_SETTINGS;
                settingsOption = 0;
            }
            if (kDown & HidNpadButton_Y) {
                for (auto& e : romList) if (e.texture) SDL_DestroyTexture(e.texture);
                romList = scanRoms(g_config.rom_path);
                selectedIndex = 0;
                lastPage = -1;
            }
            if (kDown & HidNpadButton_Plus) running = false;

        } else if (state == STATE_SETTINGS) {
            if (kDown & HidNpadButton_Up)   settingsOption = (settingsOption - 1 + 3) % 3;
            if (kDown & HidNpadButton_Down) settingsOption = (settingsOption + 1) % 3;

            if (kDown & HidNpadButton_A) {
                std::string guide = "Enter path";
                std::string initial = "";
                if (settingsOption == 0) { guide = "ROM folder path"; initial = g_config.rom_path; }
                else if (settingsOption == 1) { guide = "FAKE-08 NRO path"; initial = g_config.fake08_path; }
                else if (settingsOption == 2) { guide = "Launcher NRO path"; initial = g_config.launcher_path; }

                std::string res = swkbdInput(guide, initial);
                if (!res.empty()) {
                    if (settingsOption == 0) g_config.rom_path = res;
                    else if (settingsOption == 1) g_config.fake08_path = res;
                    else if (settingsOption == 2) g_config.launcher_path = res;
                }
            }
            if (kDown & HidNpadButton_B) {
                saveConfig();
                for (auto& e : romList) if (e.texture) SDL_DestroyTexture(e.texture);
                romList = scanRoms(g_config.rom_path);
                selectedIndex = 0;
                lastPage = -1;
                state = STATE_GRID;
            }
        }

        if (selectedIndex >= totalItems && totalItems > 0) selectedIndex = totalItems - 1;
        if (selectedIndex < 0) selectedIndex = 0;

        // =================================================================
        // RENDER
        // =================================================================
        SDL_SetRenderDrawColor(renderer, C_BG.r, C_BG.g, C_BG.b, 255);
        SDL_RenderClear(renderer);

        // --- Header ---
        drawRect(renderer, 0, 0, SCREEN_W, HEADER_H, C_HEADER);
        drawRect(renderer, 0, HEADER_H - 1, SCREEN_W, 1, C_BORDER);
        drawText(renderer, fontHeader, "PICO-8 Launcher", 20, 12, C_WHITE);
        drawText(renderer, fontBody, "Tehrzky", SCREEN_W - 90, 16, C_MUTED);

        if (state == STATE_GRID) {
            // Page indicator
            std::string pageText = "Page " + std::to_string(currentPage + 1) + " / " + std::to_string(totalPages);
            drawText(renderer, fontSmall, pageText, SCREEN_W / 2, 18, C_MUTED, true);

            // --- 6×2 Grid ---
            for (int i = 0; i < ITEMS_PER_PAGE; ++i) {
                int itemIdx = pageStart + i;
                if (itemIdx >= totalItems) break;

                int row = i / COLS;
                int col = i % COLS;
                int cx = START_X + col * (CARD_W + GAP_X);
                int cy = GRID_Y + row * (CARD_H + GAP_Y);
                bool isSel = (itemIdx == selectedIndex);

                drawRect(renderer, cx, cy, CARD_W, CARD_H, isSel ? C_SELECTED : C_PANEL);
                drawRect(renderer, cx, cy, CARD_W, CARD_H, isSel ? C_ACCENT : C_BORDER, false);

                // Thumbnail with original aspect ratio
                if (romList[itemIdx].texture) {
                    SDL_Rect dst = getThumbRect(cx, cy, romList[itemIdx].origIdx].origW, romList[itemIdx].origH);
                    SDL_RenderCopy(renderer, romList[itemIdx].texture, NULL, &dst);
                } else {
                    SDL_Rect placeholder = getThumbRect(cx, cy, 0, 0);
                    drawRect(renderer, placeholder.x, placeholder.y, placeholder.w, placeholder.h, C_BG);
                    drawRect(renderer, placeholder.x, placeholder.y, placeholder.w, placeholder.h, C_BORDER, false);
                    drawText(renderer, fontSmall, "NO ICON", cx + CARD_W/2, cy + THUMB_MAX_H/2 + 10, C_MUTED, true);
                }

                // Title
                std::string title = romList[itemIdx].filename;
                if (title.size() > 20) title = title.substr(0, 18) + "..";
                drawText(renderer, fontBody, title, cx + CARD_W/2, cy + THUMB_MAX_H + 20, C_WHITE, true);
            }

            if (romList.empty()) {
                drawText(renderer, fontHeader, "No ROMs found.", SCREEN_W/2, SCREEN_H/2 - 30, C_ERROR, true);
                drawText(renderer, fontBody, g_config.rom_path, SCREEN_W/2, SCREEN_H/2 + 20, C_MUTED, true);
            }

        } else if (state == STATE_SETTINGS) {
            drawRect(renderer, 0, 0, SCREEN_W, SCREEN_H, C_OVERLAY);

            int pw = 800, ph = 380;
            int px = (SCREEN_W - pw) / 2, py = (SCREEN_H - ph) / 2;
            drawRect(renderer, px, py, pw, ph, C_PANEL);
            drawRect(renderer, px, py, pw, ph, C_BORDER, false);
            drawText(renderer, fontHeader, "Settings", SCREEN_W/2, py + 20, C_WHITE, true);

            const char* labels[] = { "ROM Path:", "FAKE-08 Path:", "Launcher Path:" };
            const char* values[] = { g_config.rom_path.c_str(), g_config.fake08_path.c_str(), g_config.launcher_path.c_str() };

            for (int i = 0; i < 3; i++) {
                int ry = py + 90 + i * 70;
                bool sel = (settingsOption == i);
                SDL_Color labelColor = sel ? C_ACCENT : C_WHITE;
                drawRect(renderer, px + 30, ry - 5, pw - 60, 55, sel ? SDL_Color{40,40,55,255} : SDL_Color{25,25,35,255});
                drawText(renderer, fontBody, labels[i], px + 50, ry + 5, labelColor);
                drawText(renderer, fontSmall, values[i], px + 50, ry + 28, C_MUTED);
            }
            drawText(renderer, fontSmall, "A = Edit   B = Save & Back   Up/Down = Select", SCREEN_W/2, py + ph - 30, C_MUTED, true);
        }

        // --- Footer ---
        drawRect(renderer, 0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, C_FOOTER);
        drawRect(renderer, 0, SCREEN_H - FOOTER_H, SCREEN_W, 1, C_BORDER);

        std::string footer;
        if (state == STATE_GRID) {
            footer = "[A] Launch   [X] Settings   [Y] Rescan   [L/R] Page   [+] Exit";
        } else {
            footer = "[A] Edit   [B] Save & Back   [Up/Down] Select";
        }
        drawText(renderer, fontSmall, footer, 20, SCREEN_H - 28, C_MUTED);

        SDL_RenderPresent(renderer);
    }

    for (auto& e : romList) if (e.texture) SDL_DestroyTexture(e.texture);
    if (fontHeader) TTF_CloseFont(fontHeader);
    if (fontBody)   TTF_CloseFont(fontBody);
    if (fontSmall)  TTF_CloseFont(fontSmall);
    IMG_Quit();
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    romfsExit();

    return 0;
}
