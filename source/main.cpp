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
                roms.push_back({ entry.path().filename().string(), entry.path().string(), nullptr });
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
                SDL_FreeSurface(surf);
            }
        } else if (!inRange && roms[i].texture != nullptr) {
            SDL_DestroyTexture(roms[i].texture);
            roms[i].texture = nullptr;
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

int main(int argc, char* argv[]) {
    romfsInit();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);

    SDL_Window* window = SDL_CreateWindow("PICO-8 Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // Load both regular and bold fonts from romfs
    TTF_Font* fontRegular = TTF_OpenFont("romfs:/PTSans-Regular.ttf", 18);
    TTF_Font* fontBold = TTF_OpenFont("romfs:/PTSans-Bold.ttf", 22);

    loadConfig();
    std::vector<RomEntry> romList = scanRoms(g_config.rom_path);

    AppState state = STATE_GRID;
    int selectedIndex = 0;
    int settingsOption = 0;
    int lastPage = -1;
    bool running = true;

    SDL_GameController* controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            break;
        }
    }

    while (running && appletMainLoop()) {
        int itemsPerPage = 16;
        int currentPage = selectedIndex / itemsPerPage;
        int pageStart = currentPage * itemsPerPage;

        if (currentPage != lastPage && state == STATE_GRID) {
            loadPageTextures(renderer, romList, pageStart, itemsPerPage);
            lastPage = currentPage;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                Uint8 btn = event.cbutton.button;

                if (state == STATE_GRID) {
                    int totalItems = static_cast<int>(romList.size());
                    
                    if (btn == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        if (selectedIndex >= 2) selectedIndex -= 2;
                    } else if (btn == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        if (selectedIndex + 2 < totalItems) selectedIndex += 2;
                    } else if (btn == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                        if (selectedIndex % 2 == 1) selectedIndex -= 1;
                    } else if (btn == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                        if (selectedIndex % 2 == 0 && selectedIndex + 1 < totalItems) selectedIndex += 1;
                    } else if (btn == SDL_CONTROLLER_BUTTON_A) {
                        if (totalItems > 0 && selectedIndex < totalItems) {
                            launchFake08(g_config.fake08_path, romList[selectedIndex].fullPath);
                            running = false;
                        }
                    } else if (btn == SDL_CONTROLLER_BUTTON_X) {
                        state = STATE_SETTINGS;
                    } else if (btn == SDL_CONTROLLER_BUTTON_START) {
                        running = false;
                    }
                } else if (state == STATE_SETTINGS) {
                    if (btn == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        settingsOption = (settingsOption - 1 + 3) % 3;
                    } else if (btn == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        settingsOption = (settingsOption + 1) % 3;
                    } else if (btn == SDL_CONTROLLER_BUTTON_B || btn == SDL_CONTROLLER_BUTTON_X) {
                        saveConfig();
                        for (auto& entry : romList) {
                            if (entry.texture) SDL_DestroyTexture(entry.texture);
                        }
                        romList = scanRoms(g_config.rom_path);
                        selectedIndex = 0;
                        lastPage = -1;
                        state = STATE_GRID;
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 20, 20, 25, 255);
        SDL_RenderClear(renderer);

        if (state == STATE_GRID) {
            SDL_SetRenderDrawColor(renderer, 35, 35, 45, 255);
            SDL_Rect headerRect = { 0, 0, 1280, 50 };
            SDL_RenderFillRect(renderer, &headerRect);
            
            // Bold header, regular controls
            renderText(renderer, fontBold, "PICO-8 Launcher", 20, 10, { 255, 255, 255, 255 });
            renderText(renderer, fontRegular, "Press X: Settings | Press +: Exit", 920, 14, { 180, 180, 180, 255 });

            int startX = 40;
            int startY = 65;
            int itemW = 580;
            int itemH = 38;
            int gapX = 40;
            int gapY = 4;
            int thumbSize = 34;

            for (int i = 0; i < itemsPerPage; ++i) {
                int itemIdx = pageStart + i;
                if (itemIdx >= static_cast<int>(romList.size())) break;

                int row = i / 2;
                int col = i % 2;
                int x = startX + col * (itemW + gapX);
                int y = startY + row * (itemH + gapY);

                SDL_Rect boxRect = { x, y, itemW, itemH };

                if (itemIdx == selectedIndex) {
                    SDL_SetRenderDrawColor(renderer, 0, 122, 255, 255);
                } else {
                    SDL_SetRenderDrawColor(renderer, 45, 45, 55, 255);
                }

                SDL_RenderFillRect(renderer, &boxRect);

                if (romList[itemIdx].texture) {
                    SDL_Rect imgRect = { x + 2, y + 2, thumbSize, thumbSize };
                    SDL_RenderCopy(renderer, romList[itemIdx].texture, NULL, &imgRect);
                } else {
                    SDL_SetRenderDrawColor(renderer, 25, 25, 30, 255);
                    SDL_Rect imgRect = { x + 2, y + 2, thumbSize, thumbSize };
                    SDL_RenderFillRect(renderer, &imgRect);
                }

                renderText(renderer, fontRegular, romList[itemIdx].filename, x + thumbSize + 12, y + 8, { 255, 255, 255, 255 });
            }

            if (romList.empty()) {
                renderText(renderer, fontBold, "No ROMs found in target folder.", 480, 320, { 255, 100, 100, 255 });
                renderText(renderer, fontRegular, g_config.rom_path, 420, 360, { 200, 200, 200, 255 });
            }
        } else if (state == STATE_SETTINGS) {
            renderText(renderer, fontBold, "Settings", 580, 40, { 255, 255, 255, 255 });

            SDL_Color activeColor = { 0, 255, 128, 255 };
            SDL_Color inactiveColor = { 200, 200, 200, 255 };

            renderText(renderer, fontRegular, "ROM Path:", 100, 150, settingsOption == 0 ? activeColor : inactiveColor);
            renderText(renderer, fontRegular, g_config.rom_path, 300, 150, { 255, 255, 255, 255 });

            renderText(renderer, fontRegular, "FAKE-08 Path:", 100, 220, settingsOption == 1 ? activeColor : inactiveColor);
            renderText(renderer, fontRegular, g_config.fake08_path, 300, 220, { 255, 255, 255, 255 });

            renderText(renderer, fontRegular, "Launcher Path:", 100, 290, settingsOption == 2 ? activeColor : inactiveColor);
            renderText(renderer, fontRegular, g_config.launcher_path, 300, 290, { 255, 255, 255, 255 });

            renderText(renderer, fontRegular, "Press B or X to save and return", 480, 600, { 150, 150, 150, 255 });
        }

        SDL_RenderPresent(renderer);
    }

    for (auto& entry : romList) {
        if (entry.texture) SDL_DestroyTexture(entry.texture);
    }

    if (controller) SDL_GameControllerClose(controller);
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
