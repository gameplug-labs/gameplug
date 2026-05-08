# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-05-08

### Added
- **C++ Plugin Helper**: Introduced `plugin_helper.h` with a `GamePlug::Plugin` base class and `REGISTER_GAMEPLUG_PLUGIN` macro to simplify C++ plugin development.
- **Extra Resolution Enumeration**: Added `ExtraEnumeratedResolutions` support for **D3D9** and **Vulkan** (Win32) to allow custom resolutions in game settings.
- **Unified Configuration**: Introduced `GamePlug.conf` for centralized management of framework settings and resolution overrides.
- **Overlay Toggle Hotkey**: Changed the visibility toggle hotkey from `HOME` to `Ctrl + HOME` across D3D9, Vulkan, and D3D12/DX11 backends.




## [0.0.1] - 2026-05-03

### Added
- **Initial Release**: Universal plugin layer with cross-API compatibility for **Vulkan**, **DirectX 9**, and **DirectX 12**.

