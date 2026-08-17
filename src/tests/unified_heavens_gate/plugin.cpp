#include <Windows.h>

#include <atomic>
#include <cstring>

#include "_plugins.h"
#include "_dbgfunctions.h"

namespace
{
int pluginHandle;
std::atomic<unsigned long> exitCode{ 0xFFFFFFFFu };

void DebugCallback(CBTYPE type, void* callbackInfo)
{
    if(type == CB_INITDEBUG)
        exitCode = 0xFFFFFFFFu;
    else if(type == CB_EXITPROCESS)
    {
        const auto info = static_cast<PLUG_CB_EXITPROCESS*>(callbackInfo);
        if(info != nullptr && info->ExitProcess != nullptr)
            exitCode = info->ExitProcess->dwExitCode;
    }
}

bool ReadRegisters(REGDUMP_AVX512 & registers)
{
    memset(&registers, 0, sizeof(registers));
    return DbgGetRegDumpEx(&registers, sizeof(registers));
}

bool AssertCompat(int, char**)
{
    const auto functions = DbgFunctions();
    REGDUMP_AVX512 registers;
    duint r8 = 0;
    const bool ok = functions->GetActiveExecutionMode && functions->GetActiveExecutionMode() == 0 &&
                    functions->GetTargetPointerSize && functions->GetTargetPointerSize() == 4 &&
                    ReadRegisters(registers) && registers.regcontext.cip <= UINT32_MAX && registers.regcontext.csp <= UINT32_MAX &&
                    !functions->ValFromString("r8", &r8);
    return _plugin_testassert(ok, "compatibility mode exposes EIP/ESP and rejects R8");
}

bool SetGateBreakpoints(int, char**)
{
    const auto functions = DbgFunctions();
    duint gateSlot = 0;
    duint gateReturn = 0;
    DWORD gate = 0;
    char command[64] = {};
    bool ok = functions->ValFromString("unified_heavens_gate:gGateCode", &gateSlot) &&
              functions->ValFromString("unified_heavens_gate:GateReturn", &gateReturn) &&
              DbgMemRead(gateSlot, &gate, sizeof(gate));
    if(ok)
    {
        sprintf_s(command, "bp %llX, ss", static_cast<unsigned long long>(gate + 40));
        ok = DbgCmdExecDirect(command);
    }
    if(ok)
    {
        sprintf_s(command, "bp %llX, ss", static_cast<unsigned long long>(gateReturn + 5));
        ok = DbgCmdExecDirect(command);
    }
    return _plugin_testassert(ok, "mixed-mode single-shot breakpoints were installed");
}

bool AssertCompatRegisters(int, char**)
{
    REGDUMP_AVX512 registers;
    const bool ok = ReadRegisters(registers) && DWORD(registers.regcontext.cax) == 0x13572468 &&
                    DWORD(registers.regcontext.cbx) == 0x24681357;
    return _plugin_testassert(ok, "x86 control registers survived compatibility-mode steps");
}

bool AssertNative(int, char**)
{
#ifndef _WIN64
    return _plugin_testassert(false, "native Heaven's Gate checks require the x64 host");
#else
    const auto functions = DbgFunctions();
    REGDUMP_AVX512 registers;
    BASIC_INSTRUCTION_INFO instruction = {};
    BYTE opcode[16] = {};
    const bool ok = functions->GetActiveExecutionMode && functions->GetActiveExecutionMode() == 1 &&
                    functions->GetTargetPointerSize && functions->GetTargetPointerSize() == 8 &&
                    ReadRegisters(registers) &&
                    registers.regcontext.cax == 0x0102030405060708ULL &&
                    registers.regcontext.r8 == 0x1122334455667788ULL &&
                    registers.regcontext.r15 == 0x99AABBCCDDEEFF00ULL &&
                    DbgMemRead(registers.regcontext.cip, opcode, sizeof(opcode)) && opcode[0] == 0xE8 &&
                    functions->DisasmFast(opcode, registers.regcontext.cip, &instruction) && instruction.size == 5 && instruction.call;
    return _plugin_testassert(ok, "native mode exposes full x64 registers and decodes the x64 call");
#endif
}

bool AssertNativeStepOver(int, char**)
{
    const auto functions = DbgFunctions();
    REGDUMP_AVX512 registers;
    BYTE opcode = 0;
    const bool ok = functions->GetActiveExecutionMode && functions->GetActiveExecutionMode() == 1 &&
                    ReadRegisters(registers) && DbgMemRead(registers.regcontext.cip, &opcode, sizeof(opcode)) && opcode == 0xEB;
    return _plugin_testassert(ok, "x64 Step Over stopped at the instruction following the call");
}

bool AssertNativeBreakpoint(int, char**)
{
    const auto functions = DbgFunctions();
    REGDUMP_AVX512 registers;
    BYTE opcode = 0;
    const bool ok = functions->GetActiveExecutionMode && functions->GetActiveExecutionMode() == 1 &&
                    functions->GetTargetPointerSize && functions->GetTargetPointerSize() == 8 &&
                    ReadRegisters(registers) && DbgMemRead(registers.regcontext.cip, &opcode, sizeof(opcode)) && opcode == 0x90;
    return _plugin_testassert(ok, "x64 single-shot breakpoint preserved native mode");
}

bool AssertReturned(int, char**)
{
    const auto functions = DbgFunctions();
    REGDUMP_AVX512 registers;
    const bool ok = functions->GetActiveExecutionMode && functions->GetActiveExecutionMode() == 0 &&
                    functions->GetTargetPointerSize && functions->GetTargetPointerSize() == 4 &&
                    ReadRegisters(registers) && DWORD(registers.regcontext.cax) == 0xA1B2C3D4;
    return _plugin_testassert(ok, "far return restored x86 mode and EAX");
}

bool AssertExit(int, char**)
{
    return _plugin_testassert(exitCode.load() == 0, "WOW64 Heaven's Gate target exited with code 0");
}
}

extern "C" __declspec(dllexport) bool pluginit(PLUG_INITSTRUCT* init)
{
    init->pluginVersion = 1;
    init->sdkVersion = PLUG_SDKVERSION;
    strncpy_s(init->pluginName, sizeof(init->pluginName), X64DBG_TEST_NAME, _TRUNCATE);
    pluginHandle = init->pluginHandle;
    _plugin_registercallback(pluginHandle, CB_INITDEBUG, DebugCallback);
    _plugin_registercallback(pluginHandle, CB_EXITPROCESS, DebugCallback);
    _plugin_registercommand(pluginHandle, "unifiedgatecompat", AssertCompat, false);
    _plugin_registercommand(pluginHandle, "unifiedgatesetbreakpoints", SetGateBreakpoints, false);
    _plugin_registercommand(pluginHandle, "unifiedgatecompatregisters", AssertCompatRegisters, false);
    _plugin_registercommand(pluginHandle, "unifiedgatenative", AssertNative, false);
    _plugin_registercommand(pluginHandle, "unifiedgatestepover", AssertNativeStepOver, false);
    _plugin_registercommand(pluginHandle, "unifiedgatenativebp", AssertNativeBreakpoint, false);
    _plugin_registercommand(pluginHandle, "unifiedgatereturned", AssertReturned, false);
    _plugin_registercommand(pluginHandle, "unifiedgateexit", AssertExit, false);
    return true;
}

extern "C" __declspec(dllexport) void plugstop()
{
}

extern "C" __declspec(dllexport) void plugsetup(PLUG_SETUPSTRUCT*)
{
}
