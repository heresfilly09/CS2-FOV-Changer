#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <thread>

const DWORD dwLocalPlayerPawn = 0x1BEC440; // local player base
const DWORD m_pCameraServices = 0x1438; // camera services offset
const DWORD m_iFOV = 0x288; // fov value offset
const DWORD m_bIsScoped = 0x2728; // scoped status offset

// process perms we need
const DWORD PROCESS_ACCESS = PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION;

// grabs base address of a module (like client.dll)
uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (hSnap == INVALID_HANDLE_VALUE) {
        std::cerr << "Snapshot failed, error: " << GetLastError() << "\n";
        return 0;
    }

    MODULEENTRY32 modEntry;
    modEntry.dwSize = sizeof(modEntry);
    if (!Module32First(hSnap, &modEntry)) {
        std::cerr << "Can't get first module, error: " << GetLastError() << "\n";
        CloseHandle(hSnap);
        return 0;
    }

    do {
        if (_wcsicmp(modEntry.szModule, modName) == 0) {
            CloseHandle(hSnap);
            return (uintptr_t)modEntry.modBaseAddr;
        }
    } while (Module32Next(hSnap, &modEntry));

    std::cerr << "Module " << modName << " not found!\n";
    CloseHandle(hSnap);
    return 0;
}

int main() {
    // look for CS2 process
    DWORD procId = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        std::cerr << "Process snapshot failed, error: " << GetLastError() << "\n";
        return 1;
    }

    PROCESSENTRY32 procEntry;
    procEntry.dwSize = sizeof(procEntry);
    if (!Process32First(hSnap, &procEntry)) {
        std::cerr << "Can't get first process, error: " << GetLastError() << "\n";
        CloseHandle(hSnap);
        return 1;
    }

    do {
        if (_wcsicmp(procEntry.szExeFile, L"cs2.exe") == 0) {
            procId = procEntry.th32ProcessID;
            break;
        }
    } while (Process32Next(hSnap, &procEntry));

    CloseHandle(hSnap);

    if (procId == 0) {
        std::cerr << "CS2 not running!\n";
        return 1;
    }

    // open CS2 process
    HANDLE hProc = OpenProcess(PROCESS_ACCESS, FALSE, procId);
    if (hProc == NULL) {
        std::cerr << "Can't open CS2 process, error: " << GetLastError() << "\n";
        return 1;
    }

    // get client.dll base
    uintptr_t clientBase = GetModuleBaseAddress(procId, L"client.dll");
    if (clientBase == 0) {
        std::cerr << "Couldn't find client.dll!\n";
        CloseHandle(hProc);
        return 1;
    }

    // set FOV
    int desiredFov = 90;

    while (true) {
        // grab local player pawn
        uintptr_t localPawn = 0;
        if (!ReadProcessMemory(hProc, (LPCVOID)(clientBase + dwLocalPlayerPawn), &localPawn, sizeof(localPawn), nullptr)) {
            std::cerr << "Failed reading local pawn, error: " << GetLastError() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (localPawn == 0) {
            std::cerr << "Bad local pawn pointer!\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // get camera services
        uintptr_t camServices = 0;
        if (!ReadProcessMemory(hProc, (LPCVOID)(localPawn + m_pCameraServices), &camServices, sizeof(camServices), nullptr)) {
            std::cerr << "Couldn't read cam services, error: " << GetLastError() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (camServices == 0) {
            std::cerr << "Bad cam services pointer!\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // read current FOV
        int currentFov = 0;
        if (!ReadProcessMemory(hProc, (LPCVOID)(camServices + m_iFOV), &currentFov, sizeof(currentFov), nullptr)) {
            std::cerr << "FOV read failed, error: " << GetLastError() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        std::cout << "FOV: " << currentFov << "\n";

        // check if scoped
        bool isScoped = false;
        if (!ReadProcessMemory(hProc, (LPCVOID)(localPawn + m_bIsScoped), &isScoped, sizeof(isScoped), nullptr)) {
            std::cerr << "Couldn't read scoped status, error: " << GetLastError() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // set FOV if not scoped and it doesn't match
        if (!isScoped && currentFov != desiredFov) {
            if (WriteProcessMemory(hProc, (LPVOID)(camServices + m_iFOV), &desiredFov, sizeof(desiredFov), nullptr)) {
                std::cout << "Changed FOV to: " << desiredFov << "\n";
            }
            else {
                std::cerr << "FOV write failed, error: " << GetLastError() << "\n";
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // chill to avoid CPU hog
    }

    CloseHandle(hProc); // cleanup (won't hit this unless loop breaks)
    return 0;
}
