#include "threadcontext.h"

#include "console.h"
#include "debugger.h"
#include "module.h"
#include "thread.h"

#include <mutex>

namespace
{
#ifdef _WIN64
constexpr WORD Wow64CodeSelector = 0x23;
constexpr WORD NativeCodeSelector = 0x33;
std::unordered_map<DWORD, ExecutionMode> threadModes;
std::mutex threadModesMutex;
#endif

template<typename T>
T ResolveTitanContextFunction(const char* name)
{
    static HMODULE titanEngine = nullptr;
    if(titanEngine == nullptr)
    {
        titanEngine = GetModuleHandleW(L"TitanEngine.dll");
        if(titanEngine == nullptr)
            titanEngine = LoadLibraryW(L"TitanEngine.dll");
    }
    return titanEngine == nullptr ? nullptr : reinterpret_cast<T>(GetProcAddress(titanEngine, name));
}

bool TitanGetFullContext(HANDLE thread, TITAN_ENGINE_CONTEXT_t & context)
{
#ifndef _WIN64
    return GetFullContextDataEx(thread, &context);
#else
    using Function = bool(*)(HANDLE, TITAN_ENGINE_CONTEXT_t*);
    static const auto function = ResolveTitanContextFunction<Function>("GetFullContextDataEx");
    return function != nullptr && function(thread, &context);
#endif
}

bool TitanSetFullContext(HANDLE thread, TITAN_ENGINE_CONTEXT_t & context)
{
#ifndef _WIN64
    return SetFullContextDataEx(thread, &context);
#else
    using Function = bool(*)(HANDLE, TITAN_ENGINE_CONTEXT_t*);
    static const auto function = ResolveTitanContextFunction<Function>("SetFullContextDataEx");
    return function != nullptr && function(thread, &context);
#endif
}

ULONG_PTR TitanGetRegister(HANDLE thread, TitanRegister reg)
{
#ifndef _WIN64
    return GetContextDataEx(thread, reg);
#else
    using Function = ULONG_PTR(*)(HANDLE, TitanRegister);
    static const auto function = ResolveTitanContextFunction<Function>("GetContextDataEx");
    return function == nullptr ? 0 : function(thread, reg);
#endif
}

bool TitanSetRegister(HANDLE thread, TitanRegister reg, ULONG_PTR value)
{
#ifndef _WIN64
    return SetContextDataEx(thread, reg, value);
#else
    using Function = bool(*)(HANDLE, TitanRegister, ULONG_PTR);
    static const auto function = ResolveTitanContextFunction<Function>("SetContextDataEx");
    return function != nullptr && function(thread, reg, value);
#endif
}

bool IsWow64Target()
{
#ifdef _WIN64
    BOOL wow64 = FALSE;
    return fdProcessInfo != nullptr && fdProcessInfo->hProcess != nullptr &&
           IsWow64Process(fdProcessInfo->hProcess, &wow64) && wow64;
#else
    return false;
#endif
}

#ifdef _WIN64
bool GetNativeControlContext(HANDLE thread, CONTEXT & context)
{
    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL;
    if(SuspendThread(thread) == DWORD(-1))
        return false;
    const bool success = GetThreadContext(thread, &context) != FALSE;
    ResumeThread(thread);
    return success;
}

bool GetWow64Context(HANDLE thread, WOW64_CONTEXT & context)
{
    memset(&context, 0, sizeof(context));
    context.ContextFlags = WOW64_CONTEXT_ALL | WOW64_CONTEXT_EXTENDED_REGISTERS;
    if(SuspendThread(thread) == DWORD(-1))
        return false;
    const bool success = Wow64GetThreadContext(thread, &context) != FALSE;
    ResumeThread(thread);
    return success;
}

bool SetWow64Context(HANDLE thread, WOW64_CONTEXT & context)
{
    if(SuspendThread(thread) == DWORD(-1))
        return false;
    const bool success = Wow64SetThreadContext(thread, &context) != FALSE;
    ResumeThread(thread);
    return success;
}

void Wow64ToTitan(const WOW64_CONTEXT & source, TITAN_ENGINE_CONTEXT_t & target)
{
    memset(&target, 0, sizeof(target));
    target.cax = source.Eax;
    target.cbx = source.Ebx;
    target.ccx = source.Ecx;
    target.cdx = source.Edx;
    target.cdi = source.Edi;
    target.csi = source.Esi;
    target.cbp = source.Ebp;
    target.csp = source.Esp;
    target.cip = source.Eip;
    target.eflags = source.EFlags;
    target.gs = source.SegGs;
    target.fs = source.SegFs;
    target.es = source.SegEs;
    target.ds = source.SegDs;
    target.cs = source.SegCs;
    target.ss = source.SegSs;
    target.dr0 = source.Dr0;
    target.dr1 = source.Dr1;
    target.dr2 = source.Dr2;
    target.dr3 = source.Dr3;
    target.dr6 = source.Dr6;
    target.dr7 = source.Dr7;
    target.x87fpu.ControlWord = WORD(source.FloatSave.ControlWord);
    target.x87fpu.StatusWord = WORD(source.FloatSave.StatusWord);
    target.x87fpu.TagWord = WORD(source.FloatSave.TagWord);
    target.x87fpu.ErrorOffset = source.FloatSave.ErrorOffset;
    target.x87fpu.ErrorSelector = source.FloatSave.ErrorSelector;
    target.x87fpu.DataOffset = source.FloatSave.DataOffset;
    target.x87fpu.DataSelector = source.FloatSave.DataSelector;
    target.x87fpu.Cr0NpxState = source.FloatSave.Cr0NpxState;
    memcpy(target.RegisterArea, source.FloatSave.RegisterArea, sizeof(target.RegisterArea));
    memcpy(&target.MxCsr, &source.ExtendedRegisters[24], sizeof(target.MxCsr));
    for(size_t i = 0; i < 8; i++)
        memcpy(&target.XmmRegisters[i], &source.ExtendedRegisters[(10 + i) * 16], sizeof(XmmRegister_t));
}

void TitanToWow64(const TITAN_ENGINE_CONTEXT_t & source, WOW64_CONTEXT & target)
{
    target.Eax = DWORD(source.cax);
    target.Ebx = DWORD(source.cbx);
    target.Ecx = DWORD(source.ccx);
    target.Edx = DWORD(source.cdx);
    target.Edi = DWORD(source.cdi);
    target.Esi = DWORD(source.csi);
    target.Ebp = DWORD(source.cbp);
    target.Esp = DWORD(source.csp);
    target.Eip = DWORD(source.cip);
    target.EFlags = DWORD(source.eflags);
    target.SegGs = source.gs;
    target.SegFs = source.fs;
    target.SegEs = source.es;
    target.SegDs = source.ds;
    target.SegCs = source.cs;
    target.SegSs = source.ss;
    target.Dr0 = DWORD(source.dr0);
    target.Dr1 = DWORD(source.dr1);
    target.Dr2 = DWORD(source.dr2);
    target.Dr3 = DWORD(source.dr3);
    target.Dr6 = DWORD(source.dr6);
    target.Dr7 = DWORD(source.dr7);
    target.FloatSave.ControlWord = source.x87fpu.ControlWord;
    target.FloatSave.StatusWord = source.x87fpu.StatusWord;
    target.FloatSave.TagWord = source.x87fpu.TagWord;
    target.FloatSave.ErrorOffset = source.x87fpu.ErrorOffset;
    target.FloatSave.ErrorSelector = source.x87fpu.ErrorSelector;
    target.FloatSave.DataOffset = source.x87fpu.DataOffset;
    target.FloatSave.DataSelector = source.x87fpu.DataSelector;
    target.FloatSave.Cr0NpxState = source.x87fpu.Cr0NpxState;
    memcpy(target.FloatSave.RegisterArea, source.RegisterArea, sizeof(source.RegisterArea));
    memcpy(&target.ExtendedRegisters[24], &source.MxCsr, sizeof(source.MxCsr));
    for(size_t i = 0; i < 8; i++)
        memcpy(&target.ExtendedRegisters[(10 + i) * 16], &source.XmmRegisters[i], sizeof(XmmRegister_t));
}
#endif

duint RegisterValue(const TITAN_ENGINE_CONTEXT_t & context, TitanRegister reg, ExecutionMode mode, bool & supported)
{
    supported = true;
    switch(reg)
    {
    case UE_EAX: return DWORD(context.cax);
    case UE_EBX: return DWORD(context.cbx);
    case UE_ECX: return DWORD(context.ccx);
    case UE_EDX: return DWORD(context.cdx);
    case UE_EDI: return DWORD(context.cdi);
    case UE_ESI: return DWORD(context.csi);
    case UE_EBP: return DWORD(context.cbp);
    case UE_ESP: return DWORD(context.csp);
    case UE_EIP: return DWORD(context.cip);
    case UE_EFLAGS: return DWORD(context.eflags);
    case UE_CIP: return context.cip;
    case UE_CSP: return context.csp;
    case UE_RAX: return context.cax;
    case UE_RBX: return context.cbx;
    case UE_RCX: return context.ccx;
    case UE_RDX: return context.cdx;
    case UE_RDI: return context.cdi;
    case UE_RSI: return context.csi;
    case UE_RBP: return context.cbp;
    case UE_RSP: return context.csp;
    case UE_RIP: return context.cip;
    case UE_RFLAGS: return context.eflags;
#ifdef _WIN64
    case UE_R8: if(mode == ExecutionMode::X64) return context.r8; break;
    case UE_R9: if(mode == ExecutionMode::X64) return context.r9; break;
    case UE_R10: if(mode == ExecutionMode::X64) return context.r10; break;
    case UE_R11: if(mode == ExecutionMode::X64) return context.r11; break;
    case UE_R12: if(mode == ExecutionMode::X64) return context.r12; break;
    case UE_R13: if(mode == ExecutionMode::X64) return context.r13; break;
    case UE_R14: if(mode == ExecutionMode::X64) return context.r14; break;
    case UE_R15: if(mode == ExecutionMode::X64) return context.r15; break;
#endif
    case UE_DR0: return context.dr0;
    case UE_DR1: return context.dr1;
    case UE_DR2: return context.dr2;
    case UE_DR3: return context.dr3;
    case UE_DR6: return context.dr6;
    case UE_DR7: return context.dr7;
    case UE_SEG_GS: return context.gs;
    case UE_SEG_FS: return context.fs;
    case UE_SEG_ES: return context.es;
    case UE_SEG_DS: return context.ds;
    case UE_SEG_CS: return context.cs;
    case UE_SEG_SS: return context.ss;
    case UE_X87_STATUSWORD: return context.x87fpu.StatusWord;
    case UE_X87_CONTROLWORD: return context.x87fpu.ControlWord;
    case UE_X87_TAGWORD: return context.x87fpu.TagWord;
    case UE_MXCSR: return context.MxCsr;
    default: break;
    }
    supported = false;
    return 0;
}

bool SetRegisterValue(TITAN_ENGINE_CONTEXT_t & context, TitanRegister reg, duint value, ExecutionMode mode)
{
    const auto value32 = DWORD(value);
    switch(reg)
    {
    case UE_EAX: context.cax = value32; return true;
    case UE_EBX: context.cbx = value32; return true;
    case UE_ECX: context.ccx = value32; return true;
    case UE_EDX: context.cdx = value32; return true;
    case UE_EDI: context.cdi = value32; return true;
    case UE_ESI: context.csi = value32; return true;
    case UE_EBP: context.cbp = value32; return true;
    case UE_ESP: context.csp = value32; return true;
    case UE_EIP: context.cip = value32; return true;
    case UE_EFLAGS: context.eflags = value32; return true;
    case UE_CIP: context.cip = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_CSP: context.csp = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_RAX: context.cax = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_RBX: context.cbx = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_RCX: context.ccx = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_RDX: context.cdx = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_RDI: context.cdi = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_RSI: context.csi = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_RBP: context.cbp = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_RSP: context.csp = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_RIP: context.cip = mode == ExecutionMode::X86 ? value32 : value; return true;
    case UE_RFLAGS: context.eflags = mode == ExecutionMode::X86 ? value32 : value; return true;
#ifdef _WIN64
    case UE_R8: if(mode == ExecutionMode::X64) { context.r8 = value; return true; } break;
    case UE_R9: if(mode == ExecutionMode::X64) { context.r9 = value; return true; } break;
    case UE_R10: if(mode == ExecutionMode::X64) { context.r10 = value; return true; } break;
    case UE_R11: if(mode == ExecutionMode::X64) { context.r11 = value; return true; } break;
    case UE_R12: if(mode == ExecutionMode::X64) { context.r12 = value; return true; } break;
    case UE_R13: if(mode == ExecutionMode::X64) { context.r13 = value; return true; } break;
    case UE_R14: if(mode == ExecutionMode::X64) { context.r14 = value; return true; } break;
    case UE_R15: if(mode == ExecutionMode::X64) { context.r15 = value; return true; } break;
#endif
    case UE_DR0: context.dr0 = value; return true;
    case UE_DR1: context.dr1 = value; return true;
    case UE_DR2: context.dr2 = value; return true;
    case UE_DR3: context.dr3 = value; return true;
    case UE_DR6: context.dr6 = value; return true;
    case UE_DR7: context.dr7 = value; return true;
    case UE_SEG_GS: context.gs = WORD(value); return true;
    case UE_SEG_FS: context.fs = WORD(value); return true;
    case UE_SEG_ES: context.es = WORD(value); return true;
    case UE_SEG_DS: context.ds = WORD(value); return true;
    case UE_SEG_CS: context.cs = WORD(value); return true;
    case UE_SEG_SS: context.ss = WORD(value); return true;
    case UE_X87_STATUSWORD: context.x87fpu.StatusWord = WORD(value); return true;
    case UE_X87_CONTROLWORD: context.x87fpu.ControlWord = WORD(value); return true;
    case UE_X87_TAGWORD: context.x87fpu.TagWord = WORD(value); return true;
    case UE_MXCSR: context.MxCsr = DWORD(value); return true;
    default: break;
    }
    return false;
}
}

bool GetThreadExecutionMode(HANDLE thread, ExecutionMode & mode)
{
#ifdef _WIN64
    if(IsWow64Target())
    {
        const auto debugData = GetDebugData();
        const auto threadId = thread == hActiveThread && debugData != nullptr ? debugData->dwThreadId : ThreadGetId(thread);
        if(threadId && debugData != nullptr && threadId == debugData->dwThreadId)
        {
            std::lock_guard<std::mutex> lock(threadModesMutex);
            const auto found = threadModes.find(threadId);
            if(found != threadModes.end())
            {
                mode = found->second;
                return true;
            }
        }

        CONTEXT context;
        if(!GetNativeControlContext(thread, context))
        {
            if(dbgisdebugging())
                dprintf(QT_TRANSLATE_NOOP("DBG", "Could not read native thread context while determining execution mode (error %u).\n"), GetLastError());
            return false;
        }
        if(context.SegCs == Wow64CodeSelector)
            mode = ExecutionMode::X86;
        else if(context.SegCs == NativeCodeSelector)
            mode = ExecutionMode::X64;
        else
        {
            dprintf(QT_TRANSLATE_NOOP("DBG", "Unsupported code selector 0x%04X while determining execution mode.\n"), context.SegCs);
            return false;
        }
        if(threadId)
        {
            std::lock_guard<std::mutex> lock(threadModesMutex);
            threadModes[threadId] = mode;
        }
    }
    else
        mode = ExecutionMode::X64;
#else
    mode = ExecutionMode::X86;
#endif
    return true;
}

bool GetActiveExecutionMode(ExecutionMode & mode)
{
    return hActiveThread != nullptr && GetThreadExecutionMode(hActiveThread, mode);
}

bool GetExecutionModeAt(duint address, ExecutionMode & mode)
{
#ifndef _WIN64
    (void)address;
    mode = ExecutionMode::X86;
    return true;
#else
    ExecutionMode activeMode;
    const bool hasActiveMode = GetActiveExecutionMode(activeMode);
    if(hasActiveMode && (address == 0 || address == GetInstructionPointer(hActiveThread)))
    {
        mode = activeMode;
        return true;
    }
    if(ModGetExecutionMode(address, mode))
        return true;
    if(hasActiveMode)
    {
        mode = activeMode;
        return true;
    }
    return false;
#endif
}

size_t GetTargetPointerSize()
{
#ifndef _WIN64
    return 4;
#else
    ExecutionMode mode;
    return GetActiveExecutionMode(mode) && mode == ExecutionMode::X86 ? 4 : 8;
#endif
}

bool GetFullThreadContext(HANDLE thread, TITAN_ENGINE_CONTEXT_t & context)
{
    ExecutionMode mode;
    if(!GetThreadExecutionMode(thread, mode))
        return false;
#ifdef _WIN64
    if(mode == ExecutionMode::X86)
    {
        WOW64_CONTEXT wow64Context;
        if(!GetWow64Context(thread, wow64Context))
            return false;
        Wow64ToTitan(wow64Context, context);
        return true;
    }
#endif
    return TitanGetFullContext(thread, context);
}

bool SetFullThreadContext(HANDLE thread, const TITAN_ENGINE_CONTEXT_t & context)
{
    ExecutionMode mode;
    if(!GetThreadExecutionMode(thread, mode))
        return false;
#ifdef _WIN64
    if(mode == ExecutionMode::X86)
    {
        WOW64_CONTEXT wow64Context;
        if(!GetWow64Context(thread, wow64Context))
            return false;
        TitanToWow64(context, wow64Context);
        return SetWow64Context(thread, wow64Context);
    }
#endif
    auto mutableContext = context;
    return TitanSetFullContext(thread, mutableContext);
}

bool GetRegister(HANDLE thread, TitanRegister reg, duint & value)
{
    ExecutionMode mode;
    if(!GetThreadExecutionMode(thread, mode))
        return false;
    if(mode == ExecutionMode::X64)
    {
        value = TitanGetRegister(thread, reg);
        return true;
    }
    TITAN_ENGINE_CONTEXT_t context = {};
    if(!GetFullThreadContext(thread, context))
        return false;
    bool supported;
    value = RegisterValue(context, reg, mode, supported);
    return supported;
}

bool SetRegister(HANDLE thread, TitanRegister reg, duint value)
{
    ExecutionMode mode;
    if(!GetThreadExecutionMode(thread, mode))
        return false;
    if(mode == ExecutionMode::X64)
        return TitanSetRegister(thread, reg, ULONG_PTR(value));
    TITAN_ENGINE_CONTEXT_t context = {};
    if(!GetFullThreadContext(thread, context) || !SetRegisterValue(context, reg, value, mode))
        return false;
    return SetFullThreadContext(thread, context);
}

duint GetInstructionPointer(HANDLE thread)
{
    duint value = 0;
    GetRegister(thread, UE_CIP, value);
    return value;
}

duint GetStackPointer(HANDLE thread)
{
    duint value = 0;
    GetRegister(thread, UE_CSP, value);
    return value;
}

void SetThreadExecutionMode(DWORD threadId, ExecutionMode mode)
{
#ifdef _WIN64
    if(threadId)
    {
        std::lock_guard<std::mutex> lock(threadModesMutex);
        threadModes[threadId] = mode;
    }
#else
    (void)threadId;
    (void)mode;
#endif
}

void ForgetThreadExecutionMode(DWORD threadId)
{
#ifdef _WIN64
    std::lock_guard<std::mutex> lock(threadModesMutex);
    threadModes.erase(threadId);
#else
    (void)threadId;
#endif
}

void ClearThreadExecutionModes()
{
#ifdef _WIN64
    std::lock_guard<std::mutex> lock(threadModesMutex);
    threadModes.clear();
#endif
}

// Keep the TitanEngine API surface used throughout dbg, but route context access
// through the runtime mode selector. This covers the existing live-debugging
// call sites without changing address-sized data structures or unrelated code.
#ifdef _WIN64
bool GetFullContextDataEx(HANDLE thread, TITAN_ENGINE_CONTEXT_t* context)
{
    return context != nullptr && GetFullThreadContext(thread, *context);
}

bool SetFullContextDataEx(HANDLE thread, TITAN_ENGINE_CONTEXT_t* context)
{
    return context != nullptr && SetFullThreadContext(thread, *context);
}

ULONG_PTR GetContextDataEx(HANDLE thread, TitanRegister reg)
{
    duint value = 0;
    GetRegister(thread, reg, value);
    return ULONG_PTR(value);
}

bool SetContextDataEx(HANDLE thread, TitanRegister reg, ULONG_PTR value)
{
    return SetRegister(thread, reg, duint(value));
}
#endif // _WIN64
