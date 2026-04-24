#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Global States
bool g_Verbose = false; 
FILE* g_LogFile = nullptr;
// For thread safety to avoid overwriting logs with more logs all at the same time.
CRITICAL_SECTION g_FileLock;

// The pso2h export signature
typedef int (__cdecl *PSO2H_RegisterRecvAll)(void* callbackFunc, const char* pluginName);

// ---------------------------------------------------------
// Callback
// ---------------------------------------------------------
// Note: OnPacketReceived is hooked from PSO2H. It expects the following arguments IN ORDER. Obviously.
void __cdecl OnPacketReceived(void* context, uint8_t* packetData, uint32_t flags, uint32_t payloadSize) {
    uint32_t totalSize = *(uint32_t*)(packetData);
    uint16_t packetId  = *(uint16_t*)(packetData + 4);

    // Drop anything 0bytes or >10000bytes, it's probably garbage or null
    if (totalSize == 0 || totalSize > 10000 || !g_LogFile) return; 

    // Get the current time
    SYSTEMTIME st;
    GetLocalTime(&st);

    // Lock the file
    EnterCriticalSection(&g_FileLock);

    // Print header bytes w/ timestamp
    fprintf(g_LogFile, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] RECV [ID: 0x%04X] Total Size: %u bytes\n", 
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            packetId, totalSize);

    // Print payload if Verbose=Y
    if (g_Verbose) {
        for (uint32_t i = 0; i < totalSize; i++) {
            fprintf(g_LogFile, "%02X ", packetData[i]);
            if ((i + 1) % 16 == 0) {
                fprintf(g_LogFile, "\n\n");
            }
        }
        if (totalSize % 16 != 0) {
            fprintf(g_LogFile, "\n");
        }
    }
    
    // fprintf(g_LogFile, "\n");
    
    // Flush the buffer straight to disk
    fflush(g_LogFile);

    // Unlock the file
    LeaveCriticalSection(&g_FileLock);
}

// ---------------------------------------------------------
// Initialization
// ---------------------------------------------------------
DWORD WINAPI MainThread(LPVOID lpParam) {
    char cfgPath[MAX_PATH];
    GetModuleFileNameA((HMODULE)lpParam, cfgPath, MAX_PATH);
    
    // Copy the current file name/path, strip and append .ini. That's our config file.
    char* ext = strrchr(cfgPath, '.');
    if (ext) strcpy(ext, ".ini");

    // Now read it!
    char val[10];
    GetPrivateProfileStringA("Settings", "Verbose", "N", val, sizeof(val), cfgPath);
    if (val[0] == 'Y' || val[0] == 'y') g_Verbose = true;

    // Open the handle to the log file.
    g_LogFile = fopen("baka-netdump.log", "a");

    // Give pso2h.dll some time to initialize. If we go too early, it'll crash.
    Sleep(2000); 

    // Go fish
    HMODULE hPso2h = GetModuleHandleA("pso2h.dll");
    // You caught: a wild pso2h.dll handle!
    if (hPso2h) {
        // Go fishing again
        PSO2H_RegisterRecvAll RegisterRecvAll = (PSO2H_RegisterRecvAll)GetProcAddress(hPso2h, "pso2hRegisterHandlerRecvAll");
        // You caught: RegisterRecvAll! I bet it's tasty grilled over an open flame.
        if (RegisterRecvAll) {
            RegisterRecvAll((void*)OnPacketReceived, "hackerman: we're in");
        }
    }
    return 0;
}

// ---------------------------------------------------------
// Entry and exit handlers
// ---------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            // Hide our shenanigans (just in case)
            DisableThreadLibraryCalls(hModule);
            // Allocate the buffer for our logs
            InitializeCriticalSection(&g_FileLock);
            CreateThread(0, 0, MainThread, hModule, 0, 0);
            break;
            
        case DLL_PROCESS_DETACH:
            // Clean up and close the file handle on detach
            if (g_LogFile) {
                fclose(g_LogFile);
            }
            DeleteCriticalSection(&g_FileLock);
            break;
    }
    return TRUE;
}