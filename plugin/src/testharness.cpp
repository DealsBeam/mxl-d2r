// Test harness: input for unattended test runs (off unless mxl-testharness.txt sits next to the DLL).
//
// Stage 1 (this file, mode "trace"): find out which Windows input calls D2R makes. Every user32 call ends in a
// win32u syscall stub of the fixed shape
//     4c 8b d1 / b8 <ssn32> / f6 04 25 08 03 fe 7f 01 / 75 03 / 0f 05 / c3 / cd 2e / c3
// (checked byte for byte before patching, nothing rip-relative in the 16 bytes that are copied). The first 14 bytes
// become `jmp [rip+0]; dq hook`, the trampoline holds the copied 16 bytes and a jump back to stub+16.
#include "testharness.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int MaxCallers = 24;

struct Stat {
	const char* label = nullptr;
	volatile LONG64 calls = 0;
	void* callers[MaxCallers] = {};
	volatile LONG64 callerCalls[MaxCallers] = {};
	int callerCount = 0;
	uint64_t lastArgs[4] = {};
};

SRWLOCK g_lock = SRWLOCK_INIT;
char g_dir[MAX_PATH] = "";
volatile LONG g_running = 0;
HANDLE g_thread = nullptr;

using Fn = int64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t);

auto ModuleOf(void* address, char* out, size_t outSize, uint64_t* offset) noexcept -> bool {
	HMODULE module = nullptr;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        static_cast<LPCSTR>(address), &module) ||
	    module == nullptr) {
		return false;
	}
	char path[MAX_PATH] = "";
	GetModuleFileNameA(module, path, MAX_PATH);
	const char* slash = std::strrchr(path, '\\');
	std::snprintf(out, outSize, "%s", slash != nullptr ? slash + 1 : path);
	*offset = reinterpret_cast<uint64_t>(address) - reinterpret_cast<uint64_t>(module);
	return true;
}

auto IsSystemFrame(void* address) noexcept -> bool {
	char name[64];
	uint64_t offset = 0;
	if (!ModuleOf(address, name, sizeof(name), &offset)) {
		return false;
	}
	return _stricmp(name, "user32.dll") == 0 || _stricmp(name, "win32u.dll") == 0 || _stricmp(name, "ntdll.dll") == 0 ||
	       _stricmp(name, "mxl-core.dll") == 0 || _stricmp(name, "gdi32.dll") == 0 || _stricmp(name, "gdi32full.dll") == 0;
}

void Note(Stat* stat, uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0) noexcept {
	stat->lastArgs[0] = a0;
	stat->lastArgs[1] = a1;
	stat->lastArgs[2] = a2;
	stat->lastArgs[3] = a3;
	InterlockedIncrement64(&stat->calls);
	void* frames[10];
	const USHORT n = RtlCaptureStackBackTrace(1, 10, frames, nullptr);
	void* caller = n > 0 ? frames[0] : nullptr;
	for (USHORT i = 0; i < n; ++i) {
		if (!IsSystemFrame(frames[i])) {
			caller = frames[i];
			break;
		}
	}
	AcquireSRWLockExclusive(&g_lock);
	int slot = -1;
	for (int i = 0; i < stat->callerCount; ++i) {
		if (stat->callers[i] == caller) {
			slot = i;
			break;
		}
	}
	if (slot < 0 && stat->callerCount < MaxCallers) {
		slot = stat->callerCount++;
		stat->callers[slot] = caller;
	}
	if (slot >= 0) {
		++stat->callerCalls[slot];
	}
	ReleaseSRWLockExclusive(&g_lock);
}

#define HARNESS_FUNCTIONS(X)                                                                                         \
	X(NtUserGetCursorInfo) X(NtUserGetRawInputData) X(NtUserGetRawInputBuffer)                \
	X(NtUserGetAsyncKeyState) X(NtUserGetKeyboardState) X(NtUserGetMessage) \
	X(NtUserGetPointerInfoList) X(NtUserMapWindowPoints) X(NtUserRegisterRawInputDevices)      \
	X(NtUserGetMouseMovePointsEx) X(NtUserGetForegroundWindow) X(NtUserGetKeyNameText) X(NtUserSetCapture)          \
	X(NtUserGetGUIThreadInfo) X(NtUserGetClipCursor) X(NtUserSetCursor) X(NtUserGetKeyboardLayout) \
	X(NtUserTrackMouseEvent) X(NtUserGetPointerDevices) X(NtUserGetTouchInputInfo) X(NtUserGetCapture) X(NtUserSetWindowsHookEx) X(NtUserSetFocus) X(NtUserSetWindowPos) X(NtUserSetWindowLongPtr) X(NtUserSetWindowLong)

#define DECLARE(name)                                                                                               \
	Stat g_stat_##name;                                                                                             \
	Fn g_orig_##name = nullptr;                                                                                     \
	int64_t H_##name(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h, \
	                 uint64_t i, uint64_t j, uint64_t k, uint64_t l) {                                                \
		Note(&g_stat_##name, a, b, c, d);                                                                                       \
		return g_orig_##name(a, b, c, d, e, f, g, h, i, j, k, l);                                                   \
	}
HARNESS_FUNCTIONS(DECLARE)
#undef DECLARE

#define HARNESS_ALL(X) HARNESS_FUNCTIONS(X) X(NtUserGetCursorPos) X(NtUserGetKeyState) X(NtUserSetCursorPos) X(NtUserClipCursor) X(NtUserSetForegroundWindow) X(NtUserPeekMessage) X(NtUserShowWindow) X(NtUserSetActiveWindow) X(NtUserCreateWindowEx)

// Virtual input, shared with tools/game_ctl.py through the mapping "Local\mxl-testharness-input". While `active` is
// set, GetCursorPos returns the virtual client position (as screen coordinates of the game window), and GetKeyState
// reports the virtual mouse buttons and modifiers instead of the real ones. The real cursor is never touched.
struct Shared {
	uint32_t magic;  // 0x4d58 when the plugin created it
	uint32_t active;
	int32_t x;  // client coordinates of the game window
	int32_t y;
	uint32_t buttons;  // bit0 left, bit1 right
	uint32_t mods;     // bit0 shift, bit1 ctrl, bit2 alt
	uint32_t seq;
	uint32_t reserved;  // bit0: the game may not bring itself to the front (SetForegroundWindow)
	uint32_t cmdSeq;    // console command mailbox: bump cmdSeq after writing `cmd`, the plugin sets cmdDone = cmdSeq
	uint32_t cmdDone;
	int32_t cmdResult;  // 1 handled, 0 not handled, -2 crashed
	uint32_t inGame;
	uint32_t pad[4];
	char cmd[512];  // offset 64
};
Shared* g_shared = nullptr;
HWND g_hwnd = nullptr;

auto GameWindow() noexcept -> HWND {
	if (g_hwnd != nullptr && IsWindow(g_hwnd) != 0) {
		return g_hwnd;
	}
	g_hwnd = nullptr;
	EnumWindows(
	    [](HWND w, LPARAM) -> BOOL {
		    DWORD pid = 0;
		    GetWindowThreadProcessId(w, &pid);
		    char title[64] = "";
		    if (pid == GetCurrentProcessId() && IsWindowVisible(w) != 0 && GetWindowTextA(w, title, sizeof(title)) > 0 &&
		        std::strcmp(title, "Diablo II: Resurrected") == 0) {
			    g_hwnd = w;
			    return FALSE;
		    }
		    return TRUE;
	    },
	    0);
	return g_hwnd;
}

Stat g_stat_NtUserGetCursorPos;
Fn g_orig_NtUserGetCursorPos = nullptr;
int64_t H_NtUserGetCursorPos(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h,
                             uint64_t i, uint64_t j, uint64_t k, uint64_t l) {
	Note(&g_stat_NtUserGetCursorPos, a, b, c, d);
	const int64_t result = g_orig_NtUserGetCursorPos(a, b, c, d, e, f, g, h, i, j, k, l);
	if (g_shared != nullptr && g_shared->active != 0 && a != 0) {
		const HWND window = GameWindow();
		POINT pt {g_shared->x, g_shared->y};
		if (window != nullptr && ClientToScreen(window, &pt) != 0) {
			*reinterpret_cast<POINT*>(a) = pt;
		}
	}
	return result;
}

volatile LONG64 g_vkCounts[256] = {};
Stat g_stat_NtUserGetKeyState;
Fn g_orig_NtUserGetKeyState = nullptr;
int64_t H_NtUserGetKeyState(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h,
                            uint64_t i, uint64_t j, uint64_t k, uint64_t l) {
	Note(&g_stat_NtUserGetKeyState, a, b, c, d);
	InterlockedIncrement64(&g_vkCounts[a & 0xff]);
	int64_t result = g_orig_NtUserGetKeyState(a, b, c, d, e, f, g, h, i, j, k, l);
	if (g_shared != nullptr && g_shared->active != 0) {
		bool down = false;
		bool known = true;
		switch (static_cast<uint32_t>(a) & 0xff) {
		case 0x01: down = (g_shared->buttons & 1) != 0; break;
		case 0x02: down = (g_shared->buttons & 2) != 0; break;
		case 0x10: case 0xa0: case 0xa1: down = (g_shared->mods & 1) != 0; break;
		case 0x11: case 0xa2: case 0xa3: down = (g_shared->mods & 2) != 0; break;
		case 0x12: case 0xa4: case 0xa5: down = (g_shared->mods & 4) != 0; break;
		default: known = false; break;
		}
		if (known) {
			result = down ? static_cast<int64_t>(static_cast<int16_t>(0x8000)) : 0;
		}
	}
	return result;
}

// D2R moves and confines the real cursor on its own (SetCursorPos, ClipCursor): while the virtual input is active
// those calls only move the virtual cursor / do nothing, so a test run never touches the user's mouse.
// Console commands: tools/game_ctl.py writes a command into the block and bumps cmdSeq; the UI thread (the one that
// pumps the game window's messages) runs it through D2RCore's ExecuteConsoleCommand export and sets cmdDone.
using ExecuteConsoleCommandFn = bool(__fastcall*)(const char*) noexcept;
using IsInGameFn = bool(__cdecl*)() noexcept;
ExecuteConsoleCommandFn g_execute = nullptr;
IsInGameFn g_isInGame = nullptr;
volatile LONG g_inCommand = 0;
volatile LONG64 g_msgCounts[0x400] = {};
volatile LONG g_ringPos = 0;
uint64_t g_ring[32][2] = {};

auto RunCommandSafe(const char* command) noexcept -> int {
	__try {
		return g_execute(command) ? 1 : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

void ServiceCommands() noexcept {
	if (g_shared == nullptr || g_execute == nullptr || g_shared->cmdSeq == g_shared->cmdDone || g_inCommand != 0) {
		return;
	}
	const HWND window = GameWindow();
	if (window == nullptr || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) {
		return;  // only the UI thread runs console commands
	}
	g_inCommand = 1;
	const uint32_t seq = g_shared->cmdSeq;
	char command[512];
	std::memcpy(command, g_shared->cmd, sizeof(command));
	command[sizeof(command) - 1] = '\0';
	// "h <verb> ..." are the harness's own gameplay commands (harness_cmds.cpp), anything else is a console line.
	g_shared->cmdResult = (command[0] == 'h' && (command[1] == ' ' || command[1] == '\0')) ? HarnessCommand(command)
	                                                                                       : RunCommandSafe(command);
	g_shared->cmdDone = seq;
	g_inCommand = 0;
}

Stat g_stat_NtUserPeekMessage;
Fn g_orig_NtUserPeekMessage = nullptr;
int64_t H_NtUserPeekMessage(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h,
                            uint64_t i, uint64_t j, uint64_t k, uint64_t l) {
	Note(&g_stat_NtUserPeekMessage, a, b, c, d);
	ServiceCommands();
	const int64_t result = g_orig_NtUserPeekMessage(a, b, c, d, e, f, g, h, i, j, k, l);
	if ((result & 0xff) != 0 && a != 0) {
		const auto* msg = reinterpret_cast<const uint32_t*>(a);
		const uint32_t id = msg[2];  // MSG: hwnd (8), message (4), ...
		if (id < 0x400) {
			InterlockedIncrement64(&g_msgCounts[id]);
		}
		if ((id >= 0x100 && id <= 0x105) || (id >= 0x200 && id <= 0x20a && id != 0x200)) {
			const LONG pos = InterlockedIncrement(&g_ringPos) & 31;
			g_ring[pos][0] = id;
			g_ring[pos][1] = *reinterpret_cast<const uint64_t*>(a + 16);
		}
	}
	return result;
}

Stat g_stat_NtUserSetCursorPos;
Fn g_orig_NtUserSetCursorPos = nullptr;
int64_t H_NtUserSetCursorPos(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h,
                             uint64_t i, uint64_t j, uint64_t k, uint64_t l) {
	Note(&g_stat_NtUserSetCursorPos, a, b, c, d);
	if (g_shared != nullptr && g_shared->active != 0) {
		const HWND window = GameWindow();
		POINT pt {static_cast<int32_t>(a), static_cast<int32_t>(b)};
		if (window != nullptr && ScreenToClient(window, &pt) != 0) {
			g_shared->x = pt.x;
			g_shared->y = pt.y;
		}
		return 1;
	}
	return g_orig_NtUserSetCursorPos(a, b, c, d, e, f, g, h, i, j, k, l);
}

Stat g_stat_NtUserClipCursor;
Fn g_orig_NtUserClipCursor = nullptr;
int64_t H_NtUserClipCursor(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h,
                           uint64_t i, uint64_t j, uint64_t k, uint64_t l) {
	Note(&g_stat_NtUserClipCursor, a, b, c, d);
	if (g_shared != nullptr && g_shared->active != 0) {
		return 1;
	}
	return g_orig_NtUserClipCursor(a, b, c, d, e, f, g, h, i, j, k, l);
}

// Focus: with bit 0 of `reserved` set the game cannot bring itself to the front.
Stat g_stat_NtUserSetForegroundWindow;
Fn g_orig_NtUserSetForegroundWindow = nullptr;
int64_t H_NtUserSetForegroundWindow(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g,
                                    uint64_t h, uint64_t i, uint64_t j, uint64_t k, uint64_t l) {
	Note(&g_stat_NtUserSetForegroundWindow, a, b, c, d);
	if (g_shared != nullptr && (g_shared->reserved & 1) != 0 && a == reinterpret_cast<uint64_t>(GameWindow())) {
		return 1;
	}
	return g_orig_NtUserSetForegroundWindow(a, b, c, d, e, f, g, h, i, j, k, l);
}

// The loader shows the new game window with SW_SHOW and activates it, which takes the user's focus. With bit 0 of
// `reserved` set (always, while the harness is on) it is shown with SW_SHOWNA instead and SetActiveWindow does nothing.
Stat g_stat_NtUserShowWindow;
Fn g_orig_NtUserShowWindow = nullptr;
int64_t H_NtUserShowWindow(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h,
                           uint64_t i, uint64_t j, uint64_t k, uint64_t l) {
	Note(&g_stat_NtUserShowWindow, a, b, c, d);
	if (g_shared != nullptr && (g_shared->reserved & 1) != 0 && (b & 0xff) == 5) {
		b = 8;  // SW_SHOW -> SW_SHOWNA
	}
	return g_orig_NtUserShowWindow(a, b, c, d, e, f, g, h, i, j, k, l);
}

Stat g_stat_NtUserSetActiveWindow;
Fn g_orig_NtUserSetActiveWindow = nullptr;
int64_t H_NtUserSetActiveWindow(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h,
                                uint64_t i, uint64_t j, uint64_t k, uint64_t l) {
	Note(&g_stat_NtUserSetActiveWindow, a, b, c, d);
	if (g_shared != nullptr && (g_shared->reserved & 1) != 0) {
		return 0;
	}
	return g_orig_NtUserSetActiveWindow(a, b, c, d, e, f, g, h, i, j, k, l);
}

// Top-level windows get WS_EX_NOACTIVATE so the game window never becomes the foreground window by itself (it is
// created in front of the user's windows otherwise, for about a second). NtUserCreateWindowEx has 15 arguments.
using Fn15 = int64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                         uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
Stat g_stat_NtUserCreateWindowEx;
Fn15 g_orig_NtUserCreateWindowEx = nullptr;
int64_t H_NtUserCreateWindowEx(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h,
                               uint64_t i, uint64_t j, uint64_t k, uint64_t l, uint64_t m, uint64_t n, uint64_t o) {
	Note(&g_stat_NtUserCreateWindowEx, a, e, 0, 0);
	if (g_shared != nullptr && (g_shared->reserved & 1) != 0 && (e & 0x40000000U) == 0) {  // not WS_CHILD
		a |= 0x08000000U;  // WS_EX_NOACTIVATE
	}
	return g_orig_NtUserCreateWindowEx(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o);
}

auto Patch(void* stub, void* hook, void** original) noexcept -> bool {
	static const uint8_t Shape[16] = {0x4c, 0x8b, 0xd1, 0xb8, 0, 0, 0, 0, 0xf6, 0x04, 0x25, 0x08, 0x03, 0xfe, 0x7f, 0x01};
	const auto* code = static_cast<const uint8_t*>(stub);
	for (int i = 0; i < 16; ++i) {
		if (i >= 4 && i < 8) {
			continue;  // syscall number
		}
		if (code[i] != Shape[i]) {
			return false;
		}
	}
	if (code[16] != 0x75 || code[17] != 0x03 || code[18] != 0x0f || code[19] != 0x05 || code[20] != 0xc3 || code[21] != 0xcd ||
	    code[22] != 0x2e || code[23] != 0xc3) {
		return false;
	}
	auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	if (trampoline == nullptr) {
		return false;
	}
	std::memcpy(trampoline, code, 16);
	trampoline[16] = 0xff;
	trampoline[17] = 0x25;
	std::memset(trampoline + 18, 0, 4);
	const uint64_t back = reinterpret_cast<uint64_t>(stub) + 16;
	std::memcpy(trampoline + 22, &back, 8);
	DWORD old = 0;
	if (!VirtualProtect(stub, 16, PAGE_EXECUTE_READWRITE, &old)) {
		return false;
	}
	uint8_t jump[14] = {0xff, 0x25, 0, 0, 0, 0};
	const uint64_t target = reinterpret_cast<uint64_t>(hook);
	std::memcpy(jump + 6, &target, 8);
	std::memcpy(stub, jump, 14);
	VirtualProtect(stub, 16, old, &old);
	FlushInstructionCache(GetCurrentProcess(), stub, 16);
	*original = trampoline;
	return true;
}

void DumpTrace() noexcept {
	char path[MAX_PATH];
	std::snprintf(path, sizeof(path), "%smxl-testharness-trace.log", g_dir);
	FILE* f = nullptr;
	if (fopen_s(&f, path, "wb") != 0 || f == nullptr) {
		return;
	}
#define DUMP(name)                                                                                                  \
	if (g_stat_##name.label != nullptr) {                                                                            \
		std::fprintf(f, "%s calls=%lld last args %llx %llx %llx %llx\n", g_stat_##name.label,                       \
		             static_cast<long long>(g_stat_##name.calls),                                                   \
		             static_cast<unsigned long long>(g_stat_##name.lastArgs[0]),                                    \
		             static_cast<unsigned long long>(g_stat_##name.lastArgs[1]),                                    \
		             static_cast<unsigned long long>(g_stat_##name.lastArgs[2]),                                    \
		             static_cast<unsigned long long>(g_stat_##name.lastArgs[3]));                                   \
		AcquireSRWLockShared(&g_lock);                                                                              \
		for (int i = 0; i < g_stat_##name.callerCount; ++i) {                                                       \
			char module[64] = "?";                                                                                  \
			uint64_t offset = 0;                                                                                    \
			ModuleOf(g_stat_##name.callers[i], module, sizeof(module), &offset);                                    \
			std::fprintf(f, "    %lld x %s+0x%llx\n", static_cast<long long>(g_stat_##name.callerCalls[i]), module, \
			             static_cast<unsigned long long>(offset));                                                  \
		}                                                                                                           \
		ReleaseSRWLockShared(&g_lock);                                                                              \
	}
	HARNESS_ALL(DUMP)
	std::fprintf(f, "GetKeyState vk histogram:");
	for (int vk = 0; vk < 256; ++vk) {
		if (g_vkCounts[vk] != 0) {
			std::fprintf(f, " %02x:%lld", vk, static_cast<long long>(g_vkCounts[vk]));
		}
	}
	std::fprintf(f, "\nmessages the game took from PeekMessage:");
	for (int id = 0; id < 0x400; ++id) {
		if (g_msgCounts[id] != 0) {
			std::fprintf(f, " %x:%lld", id, static_cast<long long>(g_msgCounts[id]));
		}
	}
	std::fprintf(f, "\nlast key/mouse-button messages (id wparam):");
	for (int n = 0; n < 32; ++n) {
		const int idx = (g_ringPos + 1 + n) & 31;
		if (g_ring[idx][0] != 0) {
			std::fprintf(f, " %llx/%llx", static_cast<unsigned long long>(g_ring[idx][0]), static_cast<unsigned long long>(g_ring[idx][1]));
		}
	}
	std::fprintf(f, "\nExecuteConsoleCommand %s, IsInGame %s, UI-thread commands run: seq %u done %u result %d\n",
	             g_execute != nullptr ? "found" : "MISSING", g_isInGame != nullptr ? "found" : "missing",
	             g_shared != nullptr ? g_shared->cmdSeq : 0, g_shared != nullptr ? g_shared->cmdDone : 0,
	             g_shared != nullptr ? g_shared->cmdResult : 0);
#undef DUMP
	std::fclose(f);
}

DWORD WINAPI TraceThread(LPVOID) noexcept {
	while (g_running != 0) {
		Sleep(3000);
		DumpTrace();
	}
	return 0;
}

auto ReadMode(char* mode, size_t size) noexcept -> bool {
	HMODULE self = nullptr;
	char path[MAX_PATH] = "";
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCSTR>(&ReadMode), &self) ||
	    GetModuleFileNameA(self, path, MAX_PATH) == 0) {
		return false;
	}
	char* slash = std::strrchr(path, '\\');
	if (slash == nullptr) {
		return false;
	}
	slash[1] = '\0';
	std::snprintf(g_dir, sizeof(g_dir), "%s", path);
	std::snprintf(slash + 1, sizeof(path) - static_cast<size_t>(slash + 1 - path), "mxl-testharness.txt");
	FILE* f = nullptr;
	if (fopen_s(&f, path, "rb") == 0 && f != nullptr) {
		const size_t n = std::fread(mode, 1, size - 1, f);
		mode[n] = '\0';
		std::fclose(f);
		return true;
	}
	char env[64] = "";
	if (GetEnvironmentVariableA("MXL_TESTHARNESS", env, sizeof(env)) > 0) {
		std::snprintf(mode, size, "%s", env);
		return true;
	}
	return false;
}

}  // namespace

auto InstallTestHarness(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	char mode[128] = "";
	if (!ReadMode(mode, sizeof(mode))) {
		std::snprintf(report, reportSize, "off: no mxl-testharness.txt next to the plugin (normal play)");
		return false;
	}
	int hooked = 0;
	int missing = 0;
	HMODULE win32u = GetModuleHandleA("win32u.dll");
	if (win32u == nullptr) {
		win32u = LoadLibraryA("win32u.dll");
	}
#define INSTALL(name)                                                                                               \
	{                                                                                                               \
		void* stub = win32u != nullptr ? reinterpret_cast<void*>(GetProcAddress(win32u, #name)) : nullptr;          \
		if (stub != nullptr && Patch(stub, reinterpret_cast<void*>(&H_##name), reinterpret_cast<void**>(&g_orig_##name))) {                   \
			g_stat_##name.label = #name;                                                                             \
			++hooked;                                                                                               \
		} else {                                                                                                    \
			++missing;                                                                                              \
		}                                                                                                           \
	}
	HARNESS_ALL(INSTALL)
#undef INSTALL
	if (HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, "Local\\mxl-testharness-input");
	    mapping != nullptr) {
		g_shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
		if (g_shared != nullptr) {
			std::memset(g_shared, 0, sizeof(Shared));
			g_shared->magic = 0x4d58;
			// On from the first moment: the real cursor, focus and Ctrl state are none of the game's business in a test run.
			g_shared->x = 800;
			g_shared->y = 450;
			g_shared->reserved = 1;
			g_shared->active = 1;
		}
	}
	if (HMODULE core = GetModuleHandleA("D2RCore.dll"); core != nullptr) {
		FARPROC execute = GetProcAddress(core, "ExecuteConsoleCommand");
		if (execute == nullptr) {
			execute = GetProcAddress(core, MAKEINTRESOURCEA(14));
		}
		g_execute = reinterpret_cast<ExecuteConsoleCommandFn>(execute);
		g_isInGame = reinterpret_cast<IsInGameFn>(GetProcAddress(core, "IsInGame"));
	}
	HarnessCmdsInit(context->exeBase);
	g_running = 1;
	g_thread = CreateThread(nullptr, 0, TraceThread, nullptr, 0, nullptr);
	std::snprintf(report, reportSize, "on (mode '%.20s'): %d win32u input calls traced to mxl-testharness-trace.log (%d not present)",
	              mode, hooked, missing);
	return true;
}

void HarnessEcho(const char* line) noexcept {
	if (g_running == 0 || line == nullptr) {
		return;
	}
	char path[MAX_PATH];
	std::snprintf(path, sizeof(path), "%smxl-testharness-console.txt", g_dir);
	FILE* f = nullptr;
	if (fopen_s(&f, path, "ab") == 0 && f != nullptr) {
		std::fprintf(f, "%s\n", line);
		std::fclose(f);
	}
}

void StopTestHarness() noexcept {
	g_running = 0;
}
