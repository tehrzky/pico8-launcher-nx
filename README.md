# PICO-8 Launcher NX

A clean, fast homebrew ROM browser and launcher for PICO-8 cartridges on Nintendo Switch. Built with SDL2 and libnx.

![Grid](https://via.placeholder.com/800x450/101016/00c88c?text=6x2+Grid+Layout)

## Features

- **6×2 grid layout** — 12 games per screen with big thumbnails
- **Aspect-ratio preserved** — cart labels keep their original proportions (not squished to 1:1)
- **Page navigation** — L/R shoulder buttons to flip through pages
- **Bottom controls bar** — always-visible button hints
- **Settings overlay** — change ROM path, FAKE-08 path, and launcher path on-device
- **Switch on-screen keyboard** — edit paths without a PC
- **Clean names** — `.p8.png` / `.p8` extensions stripped from display
- **Pixel-perfect truncation** — long titles auto-fit with `...` so nothing overflows
- **Chain-loads FAKE-08** — press A to launch, exit FAKE-08 to return here

## Requirements

- Nintendo Switch with CFW (Atmosphere)
- [FAKE-08](https://github.com/jtothebell/fake-08) installed as a standalone `.nro`
- PICO-8 cartridge files (`.p8` or `.p8.png`) on your SD card

## Installation

1. Download the latest `pico8-launcher.nro` from [Releases](../../releases) or [Actions](../../actions)
2. Copy to your SD card:
   ```
   sdmc:/switch/pico8-launcher/pico8-launcher.nro
   sdmc:/switch/pico8-launcher/FAKE-08.nro   <-- your FAKE-08 build
   sdmc:/switch/pico8-launcher/carts/        <-- your .p8 / .p8.png files
   ```
3. Optional: place fonts in `romfs:` (PTSans-Regular.ttf, PTSans-Bold.ttf) or the app falls back to built-in rendering
4. Launch **PICO-8 Launcher NX** from the Homebrew Menu

## Controls

### Grid View

| Button | Action |
|--------|--------|
| **A** | Launch selected game |
| **X** | Open Settings |
| **Y** | Rescan ROM folder |
| **L / R** | Previous / Next page |
| **D-Pad / Left Stick** | Navigate grid |
| **+** | Exit launcher |

### Settings View

| Button | Action |
|--------|--------|
| **A** | Edit selected path (opens Switch keyboard) |
| **B** | Save config and return to grid |
| **Up / Down** | Select setting row |

## Settings

Press **X** in the grid to open settings. Three paths can be changed:

- **ROM Path** — folder scanned for `.p8` and `.p8.png` files
- **FAKE-08 Path** — location of your `FAKE-08.nro`
- **Launcher Path** — location of this launcher (used when FAKE-08 exits)

Changes are saved to `sdmc:/switch/pico8-launcher/config.txt`.

## Building

### Local (requires DevkitPro)

```bash
git clone https://github.com/tehrzky/pico8-launcher-nx.git
cd pico8-launcher-nx
mkdir build && cd build
cmake ..
make
```

### GitHub Actions

Every push to `main` triggers an automated build using the `devkitpro/devkita64` Docker image. Download the `.nro` artifact from the [Actions](../../actions) tab.

## File Structure

```
sdmc:/switch/pico8-launcher/
├── pico8-launcher.nro      <-- this app
├── FAKE-08.nro             <-- FAKE-08 player
├── config.txt              <-- saved settings
└── carts/
    ├── game1.p8.png
    ├── game2.p8.png
    └── game3.p8
```

## Credits

- **tehrzky** — Launcher UI and design
- **jtothebell** — [FAKE-08](https://github.com/jtothebell/fake-08) PICO-8 emulator
- **DevkitPro** — Switch homebrew toolchain
- **zep** — PICO-8

## License

GPL-3.0
