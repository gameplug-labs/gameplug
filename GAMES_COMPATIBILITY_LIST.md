# Game Compatibility List

## Supported Games

All games use `dinput8.dll` (or alternatively `version.dll`) by default unless specified otherwise.

| Game Name | Layer | File to Use |
|-----------|-------|-------------|
| Batman: Arkham Asylum | d3d9 | |
| Batman: Arkham City | d3d11 (x32) | |
| Batman: Arkham Knight | d3d11 | |
| Batman: Arkham Origins | d3d11 (x32) | |
| Fallout 4 | d3d11 (x64) | version.dll |
| Fallout New Vegas | d3d9 (x32) | |
| Resident Evil 4 Remake | d3d12 | |
| Resident Evil 5 | d3d9 | |
| Resident Evil 6 | d3d9, vulkan | |
| Resident Evil Revelations | d3d9 | |
| Resident Evil Revelations 2 | d3d9 | |
| The Elder Scrolls V: Skyrim - Legendary Edition | d3d9 | |
| The Elder Scrolls V: Skyrim Anniversary | d3d11 (x64) | |
| The Elder Scrolls V: Skyrim Special Edition | d3d11 (x64) | |

## Contributing to the Compatibility List

Know of a game that works with GamePlug? We'd love to add it to this list! 

To contribute:
1. **Fork** the [GamePlug repository](https://github.com/gameplug-labs/gameplug)
2. **Update** this file with the game name and compatible layer(s)
3. **Make a Pull Request** with your changes

Supported layers include:
- `d3d9` - DirectX 9 (x32 only)
- `d3d10` - DirectX 10 (x32 only)
- `d3d11` - DirectX 11 (x32 and x64)
- `d3d12` - DirectX 12 (x64 only)
- `vulkan` - Vulkan (x32 and x64)

Please ensure the game list remains in **alphabetical order** when adding new entries.
