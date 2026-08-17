#include <Windows.h>

#if defined(_M_IX86) || defined(__i386__)
extern "C" __declspec(dllexport) __declspec(naked) void BasicCallee()
{
    __asm
    {
        inc eax
        ret
    }
}

extern "C" __declspec(dllexport) __declspec(naked) void BasicStart()
{
    __asm
    {
        mov eax, 11223344h
        mov ebx, 55667788h
        call BasicCallee
        nop
        ret
    }
}
#else
extern "C" __declspec(dllexport) void BasicStart()
{
    __debugbreak();
}
#endif

int main()
{
    BasicStart();
    return 0;
}
