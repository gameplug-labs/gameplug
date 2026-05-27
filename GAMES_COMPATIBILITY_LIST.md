# Game Compatibility List

## Supported Games

| Game Name | Layer | File to Use |
|-----------|-------|-------------|
| Assassins Creed Directors Cut | d3d10 | dinput8.dll |
| Resident Evil 4 | d3d12 | dinput8.dll |
| Resident Evil 5 | d3d9 | dinput8.dll |
| Resident Evil 6 | d3d9, vulkan | dinput8.dll |
| Resident Evil Revelations | d3d9 | dinput8.dll |
| Resident Evil Revelations 2 | d3d9 | dinput8.dll |
| The Elder Scrolls V: Skyrim - Legendary Edition | d3d9 | dinput8.dll |
| The Elder Scrolls V: Skyrim Anniversary | d3d11 | dinput8.dll |
| The Elder Scrolls V: Skyrim Special Edition | d3d11 | dinput8.dll |

## Contributing to the Compatibility List

Know of a game that works with GamePlug? We'd love to add it to this list! 

To contribute:
1. **Fork** the [GamePlug repository](https://github.com/gameplug-labs/gameplug)
2. **Update** this file with the game name and compatible layer(s)
3. **Make a Pull Request** with your changes

Supported layers include:
- `d3d9` - DirectX 9
- `d3d10` - DirectX 10
- `d3d11` - DirectX 11
- `d3d12` - DirectX 12
- `vulkan` - Vulkan

Please ensure the game list remains in **alphabetical order** when adding new entries.
