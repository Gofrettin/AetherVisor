#include <iostream>
#include <Windows.h>
#include "forte_api.h"

int main()
{
	MessageBoxA(NULL, "aiojdaojdojeijfaef", "aodhaindsm", MB_OK);
	ForteVisor::SetNptHook((uintptr_t)MessageBoxA, (uint8_t*)"\xC3", 1, 'test');
// TerminateProcess(GetCurrentProcess(), 0x31);

	return 0;
}