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

bool AssertBasic(int, char**)
{
    const auto functions = DbgFunctions();
    if(!_plugin_testassert(functions->GetActiveExecutionMode && functions->GetActiveExecutionMode() == 0, "active execution mode is x86"))
        return false;
    if(!_plugin_testassert(functions->GetTargetPointerSize && functions->GetTargetPointerSize() == 4, "WOW64 pointer size is four bytes"))
        return false;

    REGDUMP_AVX512 registers = {};
    if(!_plugin_testassert(DbgGetRegDumpEx(&registers, sizeof(registers)), "WOW64 register dump is available"))
        return false;
    _plugin_logprintf("[unified] cip=%llX csp=%llX cax=%llX cbx=%llX cs=%X\n",
                      static_cast<unsigned long long>(registers.regcontext.cip),
                      static_cast<unsigned long long>(registers.regcontext.csp),
                      static_cast<unsigned long long>(registers.regcontext.cax),
                      static_cast<unsigned long long>(registers.regcontext.cbx),
                      registers.regcontext.cs);
    if(!_plugin_testassert(registers.regcontext.cip <= UINT32_MAX && registers.regcontext.csp <= UINT32_MAX, "EIP and ESP stay 32-bit"))
        return false;
    if(!_plugin_testassert(DWORD(registers.regcontext.cax) == 0x11223344 && DWORD(registers.regcontext.cbx) == 0x55667788, "x86 general registers have expected values"))
        return false;

    duint value = 0;
    if(!_plugin_testassert(!functions->ValFromString("r8", &value), "R8 is unavailable in x86 mode"))
        return false;

    BYTE opcode = 0;
    if(!_plugin_testassert(DbgMemRead(registers.regcontext.cip, &opcode, sizeof(opcode)) && opcode == 0xE8, "current instruction is the x86 call used by Step Over"))
        return false;
    return true;
}

bool AssertAfterStepOver(int, char**)
{
    const auto functions = DbgFunctions();
    REGDUMP_AVX512 registers = {};
    BYTE opcode = 0;
    const bool ok = functions->GetActiveExecutionMode && functions->GetActiveExecutionMode() == 0 &&
                    DbgGetRegDumpEx(&registers, sizeof(registers)) &&
                    DbgMemRead(registers.regcontext.cip, &opcode, sizeof(opcode));
    return _plugin_testassert(ok && opcode == 0x90 && DWORD(registers.regcontext.cax) == 0x11223345, "Step Over breakpoint stopped after the x86 call and restored the instruction");
}

bool AssertExit(int, char**)
{
    return _plugin_testassert(exitCode.load() == 0, "basic WOW64 target exited with code 0");
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
    _plugin_registercommand(pluginHandle, "unifiedbasicassert", AssertBasic, false);
    _plugin_registercommand(pluginHandle, "unifiedbasicstepoverassert", AssertAfterStepOver, false);
    _plugin_registercommand(pluginHandle, "unifiedbasicexit", AssertExit, false);
    return true;
}

extern "C" __declspec(dllexport) void plugstop()
{
}

extern "C" __declspec(dllexport) void plugsetup(PLUG_SETUPSTRUCT*)
{
}
