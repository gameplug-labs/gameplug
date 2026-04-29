#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

std::wstring GetSelfDirectory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    return std::wstring(buffer);
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        std::wcout << L"GamePlug Pro Loader - Proton-like Vulkan Injector" << std::endl;
        std::wcout << L"Usage: GamePlug.exe <target_app.exe> [arguments...]" << std::endl;
        return 1;
    }

    std::wstring selfDir = GetSelfDirectory();
    std::wstring targetExe = argv[1];
    
    // 1. Set VK_LAYER_PATH to the loader's directory
    SetEnvironmentVariableW(L"VK_LAYER_PATH", selfDir.c_str());

    // 2. Add our directory to PATH so vklayer.dll can find framework.dll
    wchar_t oldPath[4096];
    GetEnvironmentVariableW(L"PATH", oldPath, 4096);
    std::wstring newPath = selfDir + L";" + oldPath;
    SetEnvironmentVariableW(L"PATH", newPath.c_str());

    // 3. Enable the layer
    SetEnvironmentVariableW(L"VK_INSTANCE_LAYERS", L"VK_LAYER_GamePlug");


    // 3. Rebuild the command line for the child process
    std::wstring commandLine;
    for (int i = 1; i < argc; ++i) {
        commandLine += L"\"";
        commandLine += argv[i];
        commandLine += L"\" ";
    }

    std::wcout << L"[GamePlug] Injecting into: " << targetExe << std::endl;
    std::wcout << L"[GamePlug] Layer Path: " << selfDir << std::endl;

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    // 4. Set current directory to target executable's directory
    wchar_t targetDir[MAX_PATH];
    wcscpy_s(targetDir, targetExe.c_str());
    PathRemoveFileSpecW(targetDir);

    // If targetDir is empty, use NULL toinherit parent's current directory
    LPWSTR lpCurrentDir = (targetDir[0] == L'\0') ? NULL : targetDir;

    if (!CreateProcessW(
        NULL,               // executable path
        &commandLine[0],    // command line
        NULL,               // process security attributes
        NULL,               // thread security attributes
        FALSE,              // inherit handles
        0,                  // creation flags
        NULL,               // environment
        lpCurrentDir,       // current directory (Corrected for empty case)
        &si,
        &pi)) 
    {
        std::wcerr << L"[GamePlug] Error: Failed to launch process (0x" << std::hex << GetLastError() << L")" << std::endl;
        return 1;
    }


    // Wait for the game to exit
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return static_cast<int>(exitCode);
}
