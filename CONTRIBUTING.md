# Contributing to GamePlug

First off, thank you for considering contributing to GamePlug! This project aims to provide a robust, multi-API interception layer for the modding community, and your help is greatly appreciated. By participating in this project, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md).

## 🌈 How Can I Contribute?

### 🐛 Reporting Bugs
- Use the GitHub Issue Tracker.
- Provide a clear, descriptive title.
- Describe the exact steps to reproduce the issue.
- Include details about your environment:
  - **GPU**: (e.g., NVIDIA RTX 3080)
  - **OS**: (e.g., Windows 11)
  - **Game**: (e.g., Cyberpunk 2077)
  - **API**: (Vulkan, D3D9, or D3D12)
- Attach logs (check for `gameplug.log` or similar) and screenshots if possible.

### 💡 Suggesting Enhancements
- Open an issue with the `enhancement` label.
- Explain the "why" behind the suggestion and how it benefits the community.

### 🛠️ Pull Requests
1. **Fork** the repository.
2. **Create a branch** for your change (`git checkout -b feature/cool-new-thing`).
3. **Commit** your changes with descriptive messages.
4. **Push** to your fork and open a **Pull Request**.

## 💻 Development Setup

GamePlug is a C++ project managed with **CMake**.

1. **Clone with submodules**:
   ```powershell
   git clone --recursive https://github.com/RohitSoni/gameplug.git
   ```
2. **Tools**:
   - **Visual Studio 2026** (Desktop development with C++).
   - **CMake** (3.20+).
   - **Vulkan SDK** (if working on the Vulkan layer).

Refer to the [README.md](README.md) for specific build commands for x32 and x64.

## 📏 Coding Standards

- **C++ Version**: Target C++17 or later.
- **Style**: Use consistent indentation (4 spaces).
- **Naming**:
  - `PascalCase` for Classes and Structs.
  - `camelCase` or `snake_case` for variables and functions (stay consistent with the surrounding module).
  - `kPascalCase` or `UPPER_SNAKE_CASE` for constants.
- **Safety**: Game modding involves hooking into active processes. Always check for null pointers and be mindful of thread safety in the rendering/present paths.

## 📜 License

By contributing to GamePlug, you agree that your contributions will be licensed under its [MIT License](LICENSE).
