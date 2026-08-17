#ifndef _THREADCONTEXT_H
#define _THREADCONTEXT_H

#include "_global.h"
#include "TitanEngine/TitanEngine.h"

bool GetThreadExecutionMode(HANDLE thread, ExecutionMode & mode);
bool GetActiveExecutionMode(ExecutionMode & mode);
bool GetExecutionModeAt(duint address, ExecutionMode & mode);
size_t GetTargetPointerSize();
duint GetInstructionPointer(HANDLE thread);
duint GetStackPointer(HANDLE thread);
bool GetRegister(HANDLE thread, TitanRegister reg, duint & value);
bool SetRegister(HANDLE thread, TitanRegister reg, duint value);
bool GetFullThreadContext(HANDLE thread, TITAN_ENGINE_CONTEXT_t & context);
bool SetFullThreadContext(HANDLE thread, const TITAN_ENGINE_CONTEXT_t & context);
void SetThreadExecutionMode(DWORD threadId, ExecutionMode mode);
void ForgetThreadExecutionMode(DWORD threadId);
void ClearThreadExecutionModes();

#endif // _THREADCONTEXT_H
