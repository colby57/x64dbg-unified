#include <Windows.h>

#include <cstdint>
#include <cstring>

extern "C" __declspec(dllexport) unsigned char* gGateCode = nullptr;

#if defined(_M_IX86) || defined(__i386__)
extern "C" __declspec(dllexport) __declspec(naked) void GateReturn()
{
    __asm
    {
        mov eax, 0A1B2C3D4h
        nop
        ret
    }
}

extern "C" __declspec(dllexport) __declspec(naked) void GateStart()
{
    __asm
    {
        mov eax, 13572468h
        mov ebx, 24681357h
        push 33h
        push dword ptr [gGateCode]
        retf
    }
}

static bool BuildGateCode()
{
    static const unsigned char codeTemplate[] = {
        0x41, 0x57,                                                 // push r15
        0x49, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // mov r8,1122334455667788
        0x49, 0xBF, 0, 0, 0, 0, 0, 0, 0, 0,                       // mov r15,branch table
        0x48, 0xB8, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, // mov rax,0102030405060708
        0x41, 0xFF, 0x57, 0x08,                                     // call qword ptr [r15+8]
        0x90,                                                       // Step Over destination
        0xEB, 0x01,                                                 // jump over the ret
        0xC3,                                                       // called block: ret
        0x90,                                                       // x64 breakpoint location
        0x90,                                                       // instruction stepped after INT3
        0x41, 0x5F,                                                 // pop r15
        0x6A, 0x23,                                                 // push compatibility selector
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,                       // mov rax,GateReturn
        0x50,                                                       // push rax
        0x48, 0xCB,                                                 // retfq
        0, 0, 0, 0, 0, 0, 0, 0,                                   // branch table padding
        0, 0, 0, 0, 0, 0, 0, 0                                    // qword call destination
    };

    gGateCode = static_cast<unsigned char*>(VirtualAlloc(nullptr, sizeof(codeTemplate), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if(gGateCode == nullptr || reinterpret_cast<uintptr_t>(gGateCode) > UINT32_MAX)
        return false;
    memcpy(gGateCode, codeTemplate, sizeof(codeTemplate));
    const uint64_t branchTable = reinterpret_cast<uintptr_t>(gGateCode + 59);
    const uint64_t branchTarget = reinterpret_cast<uintptr_t>(gGateCode + 39);
    const uint64_t returnAddress = reinterpret_cast<uintptr_t>(&GateReturn);
    memcpy(gGateCode + 14, &branchTable, sizeof(branchTable));
    memcpy(gGateCode + 48, &returnAddress, sizeof(returnAddress));
    memcpy(gGateCode + 67, &branchTarget, sizeof(branchTarget));
    FlushInstructionCache(GetCurrentProcess(), gGateCode, sizeof(codeTemplate));
    return true;
}
#else
extern "C" __declspec(dllexport) void GateStart()
{
    __debugbreak();
}

static bool BuildGateCode()
{
    return true;
}
#endif

int main()
{
    if(!BuildGateCode())
        return 2;
    GateStart();
    return 0;
}
