#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

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

void launchFake08(const std::string& fake08Path, const std::string& romFullPath) {
    char argBuffer[1024];
    snprintf(argBuffer, sizeof(argBuffer), "\"%s\" \"%s\"", fake08Path.c_str(), romFullPath.c_str());
    envSetNextLoad(fake08Path.c_str(), argBuffer);
}

void renderText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color) {
    if (!font) return;
    SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

void renderTextCentered(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color) {
    if (!font) return;
    SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = { x - surf->w / 2, y, surf->w, surf->h };
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

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

SDL_Rect getThumbRect(int cx, int cy, int cardW, int origW, int origH) {
    int maxW = cardW - 20;
    int maxH = 160;
    if (origW <= 0 || origH <= 0) {
        return { cx + 10, cy + 10, maxW, maxH };
    }
    float scale = std::min((float)maxW / origW, (float)maxH / origH);
    int dw = (int)(origW * scale);
    int dh = (int)(origH * scale);
    int dx = cx + (cardW - dw) / 2;
    int dy = cy + 10 + (maxH - dh) / 2;
    return { dx, dy, dw, dh };
}

int main(int argc, char* argv[]) {
    romfsInit();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);

    SDL_Window* window = SDL_CreateWindow("PICO-8 Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    TTF_Font* fontRegular = TTF_OpenFont("romfs:/PTSans-Regular.ttf", 16);
    TTF_Font* fontBold = TTF_OpenFont("romfs:/PTSans-Bold.ttf", 22);

    loadConfig();
    std::vector<RomEntry> romList = scanRoms(g_config.rom_path);

    AppState state = STATE_GRID;
    int selectedIndex = 0;
    int settingsOption = 0;
    int lastPage = -1;
    bool running = true;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    const int COLS = 6;
    const int ROWS = 2;
    const int ITEMS_PER_PAGE = COLS * ROWS;
    const int CARD_W = 195;
    const int CARD_H = 290;
    const int GAP_X = 12;
    const int GAP_Y = 15;
    const int START_X = 20;
    const int GRID_Y = 60;
    const int HEADER_H = 50;
    const int FOOTER_H = 40;

    SDL_Color C_BG = { 16, 16, 22, 255 };
    SDL_Color C_HEADER = { 25, 25, 35, 255 };
    SDL_Color C_FOOTER = { 22, 22, 30, 255 };
    SDL_Color C_CARD = { 35, 35, 48, 255 };
    SDL_Color C_CARD_SEL = { 45, 55, 75, 255 };
    SDL_Color C_BORDER = { 60, 60, 80, 255 };
    SDL_Color C_ACCENT = { 0, 200, 140, 255 };
    SDL_Color C_WHITE = { 255, 255, 255, 255 };
    SDL_Color C_MUTED = { 140, 140, 160, 255 };
    SDL_Color C_ERROR = { 220, 60, 60, 255 };
    SDL_Color C_OVERLAY = { 10, 10, 16, 240 };

    while (running && appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        int totalItems = static_cast<int>(romList.size());
        int totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        if (totalPages < 1) totalPages = 1;
        int currentPage = selectedIndex / ITEMS_PER_PAGE;
        int pageStart = currentPage * ITEMS_PER_PAGE;

        if (currentPage != lastPage && state == STATE_GRID) {
            loadPageTextures(renderer, romList, pageStart, ITEMS_PER_PAGE);
            lastPage = currentPage;
        }

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
                for (auto& entry : romList) if (entry.texture) SDL_DestroyTexture(entry.texture);
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
                for (auto& entry : romList) if (entry.texture) SDL_DestroyTexture(entry.texture);
                romList = scanRoms(g_config.rom_path);
                selectedIndex = 0;
                lastPage = -1;
                state = STATE_GRID;
            }
        }

        if (selectedIndex >= totalItems && totalItems > 0) selectedIndex = totalItems - 1;
        if (selectedIndex < 0) selectedIndex = 0;
        if (totalItems == 0) selectedIndex = 0;

        SDL_SetRenderDrawColor(renderer, C_BG.r, C_BG.g, C_BG.b, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, C_HEADER.r, C_HEADER.g, C_HEADER.b, 255);
        SDL_Rect headerRect = { 0, 0, 1280, HEADER_H };
        SDL_RenderFillRect(renderer, &headerRect);

        SDL_SetRenderDrawColor(renderer, C_BORDER.r, C_BORDER.g, C_BORDER.b, 255);
        SDL_Rect headerLine = { 0, HEADER_H - 1, 1280, 1 };
        SDL_RenderFillRect(renderer, &headerLine);

        renderText(renderer, fontBold, "PICO-8 Launcher", 20, 10, C_WHITE);
        renderText(renderer, fontRegular, "By Tehrzky", 1160, 16, C_MUTED);

        std::string pageText = "Page " + std::to_string(currentPage + 1) + " / " + std::to_string(totalPages);
        renderTextCentered(renderer, fontRegular, pageText, 640, 16, C_MUTED);

        if (state == STATE_GRID) {
            for (int i = 0; i < ITEMS_PER_PAGE; ++i) {
                int itemIdx = pageStart + i;
                if (itemIdx >= totalItems) break;

                int row = i / COLS;
                int col = i % COLS;
                int cx = START_X + col * (CARD_W + GAP_X);
                int cy = GRID_Y + row * (CARD_H + GAP_Y);
                bool isSel = (itemIdx == selectedIndex);

                SDL_SetRenderDrawColor(renderer, isSel ? C_CARD_SEL.r : C_CARD.r,
                                                 isSel ? C_CARD_SEL.g : C_CARD.g,
                                                 isSel ? C_CARD_SEL.b : C_CARD.b, 255);
                SDL_Rect cardRect = { cx, cy, CARD_W, CARD_H };
                SDL_RenderFillRect(renderer, &cardRect);

                SDL_SetRenderDrawColor(renderer, isSel ? C_ACCENT.r : C_BORDER.r,
                                                 isSel ? C_ACCENT.g : C_BORDER.g,
                                                 isSel ? C_ACCENT.b : C_BORDER.b, 255);
                SDL_RenderDrawRect(renderer, &cardRect);

                if (romList[itemIdx].texture) {
                    SDL_Rect imgRect = getThumbRect(cx, cy, CARD_W, romList[itemIdx].origW, romList[itemIdx].origH);
                    SDL_RenderCopy(renderer, romList[itemIdx].texture, NULL, &imgRect);
                } else {
                    SDL_Rect placeholder = getThumbRect(cx, cy, CARD_W, 0, 0);
                    SDL_SetRenderDrawColor(renderer, 20, 20, 25, 255);
                    SDL_RenderFillRect(renderer, &placeholder);
                    SDL_SetRenderDrawColor(renderer, C_BORDER.r, C_BORDER.g, C_BORDER.b, 255);
                    SDL_RenderDrawRect(renderer, &placeholder);
                    renderTextCentered(renderer, fontRegular, "NO ICON", cx + CARD_W / 2, cy + 80, C_MUTED);
                }

                std::string title = romList[itemIdx].filename;
                if (title.size() > 22) title = title.substr(0, 20) + "..";
                renderTextCentered(renderer, fontRegular, title, cx + CARD_W / 2, cy + CARD_H - 28, C_WHITE);
            }

            if (romList.empty()) {
                renderTextCentered(renderer, fontBold, "No ROMs found.", 640, 320, C_ERROR);
                renderTextCentered(renderer, fontRegular, g_config.rom_path, 640, 360, C_MUTED);
            }

        } else if (state == STATE_SETTINGS) {
            SDL_SetRenderDrawColor(renderer, C_OVERLAY.r, C_OVERLAY.g, C_OVERLAY.b, C_OVERLAY.a);
            SDL_Rect overlay = { 0, 0, 1280, 720 };
            SDL_RenderFillRect(renderer, &overlay);

            int pw = 800, ph = 380;
            int px = (1280 - pw) / 2, py = (720 - ph) / 2;

            SDL_SetRenderDrawColor(renderer, C_CARD.r, C_CARD.g, C_CARD.b, 255);
            SDL_Rect panel = { px, py, pw, ph };
            SDL_RenderFillRect(renderer, &panel);

            SDL_SetRenderDrawColor(renderer, C_BORDER.r, C_BORDER.g, C_BORDER.b, 255);
            SDL_RenderDrawRect(renderer, &panel);

            renderTextCentered(renderer, fontBold, "Settings", 640, py + 20, C_WHITE);

            const char* labels[] = { "ROM Path:", "FAKE-08 Path:", "Launcher Path:" };
            const char* values[] = { g_config.rom_path.c_str(), g_config.fake08_path.c_str(), g_config.launcher_path.c_str() };

            for (int i = 0; i < 3; i++) {
                int ry = py + 90 + i * 80;
                bool sel = (settingsOption == i);

                SDL_SetRenderDrawColor(renderer, sel ? 50 : 30, sel ? 55 : 30, sel ? 70 : 40, 255);
                SDL_Rect rowRect = { px + 30, ry - 8, pw - 60, 65 };
                SDL_RenderFillRect(renderer, &rowRect);

                SDL_Color labelColor = sel ? C_ACCENT : C_WHITE;
                renderText(renderer, fontRegular, labels[i], px + 50, ry, labelColor);
                renderText(renderer, fontRegular, values[i], px + 50, ry + 28, C_MUTED);
            }

            renderTextCentered(renderer, fontRegular, "A = Edit   B = Save & Back   Up/Down = Select", 640, py + ph - 30, C_MUTED);
        }

        SDL_SetRenderDrawColor(renderer, C_FOOTER.r, C_FOOTER.g, C_FOOTER.b, 255);
        SDL_Rect footerRect = { 0, 720 - FOOTER_H, 1280, FOOTER_H };
        SDL_RenderFillRect(renderer, &footerRect);

        SDL_SetRenderDrawColor(renderer, C_BORDER.r, C_BORDER.g, C_BORDER.b, 255);
        SDL_Rect footerLine = { 0, 720 - FOOTER_H, 1280, 1 };
        SDL_RenderFillRect(renderer, &footerLine);

        std::string footer;
        if (state == STATE_GRID) {
            footer = "[A] Launch   [X] Settings   [Y] Rescan   [L/R] Page   [+] Exit";
        } else {
            footer = "[A] Edit   [B] Save & Back   [Up/Down] Select";
        }
        renderText(renderer, fontRegular, footer, 20, 720 - 28, C_MUTED);

        SDL_RenderPresent(renderer);
    }

    for (auto& entry : romList) {
        if (entry.texture) SDL_DestroyTexture(entry.texture);
    }

    if (fontRegular) TTF_CloseFont(fontRegular);
    if (fontBold) TTF_CloseFont(fontBold);

    IMG_Quit();
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    romfsExit();

    return 0;
}
