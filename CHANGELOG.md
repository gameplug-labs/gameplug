# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.7] - 2026-07-17

### Added
- Added `winmm.dll` proxy builds for Vulkan and D3D9, D3D10, D3D11, and D3D12.
- Added MinHook-based Vulkan loader interception, avoiding Vulkan-layer registration conflicts with overlays such as RivaTuner Statistics Server (RTSS).

### Changed
- Consolidated the `dinput8.dll`, `version.dll`, and `winmm.dll` proxy implementations into shared sources where backend behavior is identical.
- Upgraded C++ standard (`CMAKE_CXX_STANDARD`) to C++23 for the main project.
- Change Shortcut key to ctrl + f1 or ~
- UI updated

### Removed
- Removed the legacy Vulkan-layer environment setup (`VK_LAYER_PATH` and `VK_INSTANCE_LAYERS`) from the D3D9 framework.
- Removed unused Vulkan layer-negotiation callbacks from the MinHook-based interception path.

## [1.0.6] - 2026-07-02

### Added
- Added `Ctrl + End` as an alternative shortcut to toggle overlay visibility.

## [1.0.5] - 2026-07-01

### Added
- Added tilde key (`~`) as an alternative shortcut to toggle overlay visibility.

## [1.0.4] - 2026-06-20

### Added
- Added proxy DLL support via `version.dll` for the D3D9 backend.
- Added D3D12 overlay support for The Witcher 3.
- Removed `run_game.bat` and `GAMEPLUG.exe` in favor of `dinput8.dll` and `version.dll` proxy DLLs for Vulkan layer loading.
- Added autoloading of the Vulkan layer into the D3D9 layer.

## [1.0.3] - 2026-06-10

### Added
- Added D3D9 Texture Replacer & Dumper module managed via `GamePlug.conf`.
- Redesigned the D3D9 ImGui overlay with collapsible "General" and "Texture Replacer & Dumper" headings, hiding sub-options when inactive.
- Optimized D3D9 performance by skipping CRC32 texture hash calculations when both replacement and dumping are disabled.


## [1.0.2] - 2026-06-09

### Added
- Added proxy DLL support via `version.dll` for all renderer backends (D3D9, D3D9 Skyrim, D3D10, D3D11, and D3D12).
- Added support for 32-bit (x32) D3D11 architecture.


## [1.0.1] - 2026-05-28

### Fixed
- Fix UI not interactable on some games.


## [1.0.0] - 2026-05-26

### Added
- DirectX 10 (D3D10) support and release packaging.
- Logs disabled by default, enabled with `Debug=true` in `GamePlug.conf`.

### Changed
- Redesigned resolution enumeration UI with dynamic checkbox toggles and bidirectional textbox synchronization.

## [0.1.2] - 2026-05-26

### Added
- Separated D3D11 and D3D12 codebases & build
- Added GAMES_COMPATIBILITY_LIST.md

## [0.1.1] - 2026-05-22

- Disabled `ExtraEnumeratedResolutions` in D3D12.
- Format and refactor code

## [0.1.0] - 2026-05-08

### Added
- **C++ Plugin Helper**: Introduced `plugin_helper.h` with a `GamePlug::Plugin` base class and `REGISTER_GAMEPLUG_PLUGIN` macro to simplify C++ plugin development.
- **Extra Resolution Enumeration**: Added `ExtraEnumeratedResolutions` support for **D3D9** and **Vulkan** (Win32) to allow custom resolutions in game settings.
- **Unified Configuration**: Introduced `GamePlug.conf` for centralized management of framework settings and resolution overrides.
- **Overlay Toggle Hotkey**: Changed the visibility toggle hotkey from `HOME` to `Ctrl + HOME` across D3D9, Vulkan, and D3D12/DX11 backends.




## [0.0.1] - 2026-05-03

### Added
- **Initial Release**: Universal plugin layer with cross-API compatibility for **Vulkan**, **DirectX 9**, and **DirectX 12**.

