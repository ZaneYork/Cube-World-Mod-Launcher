#include <iostream>
#include <windows.h>
#include <vector>
#include "DLL.h"
#include "crc.h"

#define MOD_MAJOR_VERSION 4
#define MOD_MINOR_VERSION 2

#define CUBE_VERSION "1.0.0-1"
#define CUBE_PACKED_CRC 0xC7682619
#define CUBE_UNPACKED_CRC 0xBA092543
#define CUBE_UNPACKED_CN_CRC 0x937822A8

#define MODLOADER_NAME "CubeModLoader"


#define no_optimize __attribute__((optimize("O0")))

#define MUST_IMPORT(dllname, name)\
dllname->name = GetProcAddress(dllname->handle, #name);\
            if (!dllname->name) {\
                char ERROR_MESSAGE_POPUP[512] = {0};\
                sprintf(ERROR_MESSAGE_POPUP, "%s does not export " #name ".\n", dllname->fileName.c_str());\
                Popup("Error", ERROR_MESSAGE_POPUP);\
                exit(1);\
            }

#define IMPORT(dllname, name)\
dllname->name = GetProcAddress(dllname->handle, #name);

#define PUSH_ALL "push rax\npush rbx\npush rcx\npush rdx\npush rsi\npush rdi\npush rbp\npush r8\npush r9\npush r10\npush r11\npush r12\npush r13\npush r14\npush r15\n"
#define POP_ALL "pop r15\npop r14\npop r13\npop r12\npop r11\npop r10\npop r9\npop r8\npop rbp\npop rdi\npop rsi\npop rdx\npop rcx\npop rbx\npop rax\n"

#define PREPARE_STACK "mov rax, rsp \n and rsp, 0xFFFFFFFFFFFFFFF0 \n push rax \n sub rsp, 0x28 \n"
#define RESTORE_STACK "add rsp, 0x28 \n pop rsp \n"


using namespace std;

void* base; // Module base
vector <DLL*> modDLLs; // Every mod we've loaded
HMODULE hSelf; // A handle to ourself, to prevent being unloaded
void* initterm_e; // A pointer to a function which is run extremely soon after starting, or after being unpacked
const size_t BYTES_TO_MOVE = 14; // The size of a far jump
char initterm_e_remember[BYTES_TO_MOVE]; // We'll use this to store the code we overwrite in initterm_e, so we can put it back later.

void WriteFarJMP(void* source, void* destination) {
    DWORD dwOldProtection;
    VirtualProtect(source, 14, PAGE_EXECUTE_READWRITE, &dwOldProtection);
    char* location = (char*)source;

    // Far jump
    *((UINT16*)&location[0]) = 0x25FF;

    // mode
    *((UINT32*)&location[2]) = 0x00000000;

    *((UINT64*)&location[6]) = (UINT64)destination;

    VirtualProtect(location, 14, dwOldProtection, &dwOldProtection);
}

#include "callbacks/ChatHandler.h"
#include "callbacks/P2PRequestHandler.h"
#include "callbacks/CheckInventoryFullHandler.h"

void SetupHandlers() {
    SetupChatHandler();
    SetupP2PRequestHandler();
    SetupCheckInventoryFullHandler();
}

void Popup(const char* title, const char* msg ) {
    MessageBoxA(0, msg, title, MB_OK | MB_ICONINFORMATION);
}

void PrintLoadedMods() {
    std::string mods("Mods Loaded:\n");
    for (DLL* dll : modDLLs) {
        mods += dll->fileName;
        mods += "\n";
    }
    Popup("Loaded Mods", mods.c_str());
}

// Handles injecting callbacks and the mods
void StartMods() {
    char msg[256] = {0};

    SetupHandlers();

    //Find mods
    HANDLE hFind;
    WIN32_FIND_DATA data;

    CreateDirectory("Mods", NULL);
    hFind = FindFirstFile("Mods\\*.dll", &data);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            // We should be loaded into the application's address space, so we can just LoadLibraryA
            DLL* dll = new DLL(string("Mods\\") + data.cFileName);
            dll->Load();
            printf("Loaded %s\n", dll->fileName.c_str());
            modDLLs.push_back(dll);
        } while (FindNextFile(hFind, &data));
        FindClose(hFind);
    }

    // Find all the functions the mods may export
    for (DLL* dll: modDLLs) {
        MUST_IMPORT(dll, ModMajorVersion);
        MUST_IMPORT(dll, ModMinorVersion);
        MUST_IMPORT(dll, ModPreInitialize);
        IMPORT(dll, ModInitialize);
        IMPORT(dll, HandleChat);
        IMPORT(dll, HandleP2PRequest);
        IMPORT(dll, HandleCheckInventoryFull);
        IMPORT(dll, HandleCheckMapIconVisibility);
    }

    // Ensure version compatibility
    for (DLL* dll: modDLLs) {
        int majorVersion = ((int(*)())dll->ModMajorVersion)();
        int minorVersion = ((int(*)())dll->ModMinorVersion)();
        if (majorVersion != MOD_MAJOR_VERSION) {
            sprintf(msg, "%s has major version %d but requires %d.\n", dll->fileName.c_str(), majorVersion, MOD_MAJOR_VERSION);
            Popup("Error", msg);
            exit(1);

            if (minorVersion > MOD_MINOR_VERSION) {
                sprintf(msg, "%s has minor version %d but requires %d or lower.\n", dll->fileName.c_str(), minorVersion, MOD_MINOR_VERSION);
                Popup("Error", msg);
                exit(1);
            }
        }

        // Run Initialization routines on all mods
        for (DLL* dll: modDLLs) {
            ((void(*)())dll->ModPreInitialize)();
        }

        for (DLL* dll: modDLLs) {
            if (dll->ModInitialize) {
                ((void(*)())dll->ModInitialize)();
            }
        }
    }
    if (hSelf) PrintLoadedMods();
    return;
}
void* StartMods_ptr = (void*)&StartMods;

void no_optimize ASMStartMods() {
    asm(PUSH_ALL
        PREPARE_STACK

        // Initialize mods and callbacks
        "call [StartMods_ptr] \n"

        // We can put initterm_e back how we found it.
        "call [CopyInitializationBack_ptr] \n"

        RESTORE_STACK
        POP_ALL

        // Run initterm_e properly this time.
        "jmp [initterm_e] \n"
        );
}

void PatchFreeImage(){
    // Thanks to frognik for showing off this method!
    DWORD oldProtect;
    void* patchaddr = (void*)GetModuleHandleA("FreeImage.dll") + 0x1E8C12;
    VirtualProtect((LPVOID)patchaddr, 8, PAGE_EXECUTE_READWRITE, &oldProtect);
    *(uint64_t*)patchaddr = 0x909090000000A8E9;
}

void InitializationPatch() {
    // Get pointer to initterm_e
    initterm_e = *(void**)(base + 0x42CBD8);

    // Store old code, we'll copy it back once we regain control.
    memcpy(initterm_e_remember, initterm_e, BYTES_TO_MOVE);

    // Write a jump to our code
    WriteFarJMP(initterm_e, (void*)&ASMStartMods);
}

// This restores initterm_e to how it was before we hijacked it.
void CopyInitializationBack() {
    DWORD dwOldProtection;
    VirtualProtect(initterm_e, BYTES_TO_MOVE, PAGE_EXECUTE_READWRITE, &dwOldProtection);

    memcpy(initterm_e, initterm_e_remember, BYTES_TO_MOVE);

    VirtualProtect(initterm_e, BYTES_TO_MOVE, dwOldProtection, &dwOldProtection);

    return;
}
void* CopyInitializationBack_ptr = (void*)&CopyInitializationBack;

bool already_ran = false;
extern "C" __declspec(dllexport) BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        base = GetModuleHandle(NULL);

        // Don't allow this to run more than once
        if (already_ran)
            return true;

        already_ran = true;

        char msg[256] = {0};

        // This serves to prevent ourself from being unloaded, if we are a .fip
        hSelf = LoadLibrary(MODLOADER_NAME ".fip");

        // The user could also inject this as a DLL, so if we can't find our .fip, we were probably launched with a launcher.
        // Therefore, we don't need to prompt the user.
        if (hSelf) {
            if (MessageBoxA(NULL, "Would you like to run with mods?", "Cube World Mod Loader", MB_YESNO) != IDYES) {
                PatchFreeImage();
                return true;
            }
        }

        base = GetModuleHandle(NULL);


        // Figure out where the executable is and verify its checksum
        char cubePath[_MAX_PATH+1];
        GetModuleFileName(NULL, cubePath, _MAX_PATH);

        uint32_t checksum = crc32_file(cubePath);
        if (checksum == CUBE_PACKED_CRC || checksum == CUBE_UNPACKED_CRC || checksum == CUBE_UNPACKED_CN_CRC) {
            // Patch some code to run StartMods. This method makes it work with AND without SteamStub.
            InitializationPatch();
        } else {
            sprintf(msg, "%s does not seem to be version %s. CRC %08X", cubePath, CUBE_VERSION, checksum);
            Popup("Error", msg);
            PatchFreeImage();
            return true;
        }

        Sleep(250);
        PatchFreeImage();
    }
    return true;
}
