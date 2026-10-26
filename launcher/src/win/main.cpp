// Median XL launcher: a small window (status, progress, Play / Update / Repair / Uninstall, saves folder, bug
// reports) or, with --cli, the same actions from a terminal (used by the tests, docs/launcher.md "Command line").
#include "app.h"
#include "game.h"
#include "http.h"
#include "util.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <memory>
#include <thread>

using namespace mxl;

namespace {

const wchar_t* Usage =
	L"MedianXL-Launcher.exe [--cli status|detect|install|update|repair|play|uninstall] [options]\n"
	L"  --data-dir DIR   state, cache and log (default %LOCALAPPDATA%\\MedianXL)\n"
	L"  --feed URL       release manifest (https://, http:// or file://)\n"
	L"  --source DIR     the D2R folder to copy (default: found via Battle.net / Steam)\n"
	L"  --game-dir DIR   the Median XL game copy (default: on the D2R drive)\n"
	L"  --any-drive      allow a game copy on another drive than D2R\n"
	L"  --args \"...\"     more game arguments, e.g. -w\n"
	L"  --background     start the game without taking the foreground (tests)\n"
	L"  --loader-url URL another source of the pinned D2RLoader zip (its sha256 is still checked)\n";

struct Cli {
	std::wstring command;
	Options opt;
	bool bad = false;
};

Cli ParseArgs() {
	Cli c;
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; i < argc; ++i) {
		std::wstring a = argv[i];
		auto next = [&]() -> std::wstring {
			if (i + 1 < argc)
				return argv[++i];
			c.bad = true;
			return L"";
		};
		if (a == L"--cli") c.command = next();
		else if (a == L"--data-dir") c.opt.dataDir = next();
		else if (a == L"--feed") c.opt.feed = Utf8(next());
		else if (a == L"--source") c.opt.source = next();
		else if (a == L"--game-dir") c.opt.gameDir = next();
		else if (a == L"--any-drive") c.opt.anyDrive = true;
		else if (a == L"--args") c.opt.extraArgs = next();
		else if (a == L"--background") c.opt.background = true;
		else if (a == L"--loader-url") c.opt.loaderUrl = Utf8(next());
		else c.bad = true;
	}
	LocalFree(argv);
	return c;
}

void SetupLog(const Options& o, bool console) {
	std::wstring data = o.dataDir.empty() ? JoinPath(KnownFolder(0), L"MedianXL") : o.dataDir;
	LogInit(JoinPath(data, L"launcher.log"), console);
}

int RunCli(Cli& c) {
	if (!AttachConsole(ATTACH_PARENT_PROCESS))
		AllocConsole();
	SetupLog(c.opt, true);
	Log(std::string("Median XL launcher ") + LauncherVersion + " --cli " + Utf8(c.command));
	if (c.command == L"detect") {
		auto found = FindD2RInstalls();
		if (found.empty())
			Log("no D2R install found");
		for (const auto& d : found)
			Log(Utf8(d.dir) + " | " + d.source + " | .build.info " + d.infoVersion + " | D2R.exe " + d.exeVersion + " | " + d.build.message);
		return found.empty() ? 1 : 0;
	}
	Launcher l(c.opt);
	std::string last;
	l.onProgress = [&](const Progress& p) {
		if (p.fraction >= 0 && p.text != last) {  // Report() already logged the plain messages
			static ULONGLONG t = 0;
			if (GetTickCount64() - t > 3000) {
				t = GetTickCount64();
				Log(p.text);
			}
		}
		last = p.text;
	};
	std::string err;
	bool ok = true;
	if (c.command == L"status") {
		l.CheckFeed(err);
		Log(l.StatusText());
		if (!err.empty())
			Log("feed: " + err);
		return 0;
	}
	if (c.command == L"install" || c.command == L"update") ok = l.InstallOrUpdate(err);
	else if (c.command == L"repair") ok = l.Repair(err);
	else if (c.command == L"play") ok = l.Play(err);
	else if (c.command == L"uninstall") ok = l.Uninstall(err);
	else {
		Log(Utf8(Usage));
		return 2;
	}
	Log(ok ? "OK" : "FAILED: " + err);
	return ok ? 0 : 1;
}

// ---------------------------------------------------------------- window

enum : int { IdPlay = 101, IdUpdate, IdRepair, IdUninstall, IdSaves, IdIssues, IdChoose, IdCancel };
constexpr UINT WmProgress = WM_APP + 1, WmDone = WM_APP + 2;

struct Ui {
	HWND wnd = nullptr, status = nullptr, line = nullptr, bar = nullptr;
	HWND buttons[8] = {};
	HFONT font = nullptr;
	std::unique_ptr<Launcher> launcher;
	Options opt;
	bool busy = false;
	std::thread worker;
};
Ui g;

struct ProgressMsg {
	std::string text;
	double fraction;
};

void SetBusy(bool busy) {
	g.busy = busy;
	for (int i = 0; i < 7; ++i)
		EnableWindow(g.buttons[i], !busy);
	ShowWindow(g.buttons[7], busy ? SW_SHOW : SW_HIDE);
}

void RefreshStatus() {
	std::string why;
	SetWindowTextW(g.status, Wide(g.launcher->StatusText()).c_str());
	EnableWindow(g.buttons[0], !g.busy && g.launcher->CanPlay(&why));
}

void RunAction(int id) {
	if (g.busy)
		return;
	if (id == IdUninstall &&
	    MessageBoxW(g.wnd, L"Remove the Median XL game copy and the launcher's downloads?\n\nYour characters and your D2R install are not touched.",
	                L"Uninstall Median XL", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;
	SetBusy(true);
	if (g.worker.joinable())
		g.worker.join();
	g.worker = std::thread([id] {
		std::string err;
		bool ok = false;
		Launcher& l = *g.launcher;
		switch (id) {
		case IdPlay: ok = (l.CanPlay() || l.InstallOrUpdate(err)) && l.Play(err); break;  // Play works offline once installed
		case IdUpdate: ok = l.InstallOrUpdate(err); break;
		case IdRepair: ok = l.Repair(err); break;
		case IdUninstall: ok = l.Uninstall(err); break;
		}
		auto* msg = new ProgressMsg{ok ? std::string(id == IdPlay ? "Have fun." : "Done.") : err, ok ? 1.0 : -2.0};
		PostMessageW(g.wnd, WmDone, WPARAM(ok), reinterpret_cast<LPARAM>(msg));
	});
}

void ChooseSource() {
	IFileOpenDialog* dlg = nullptr;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
		return;
	DWORD flags = 0;
	dlg->GetOptions(&flags);
	dlg->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
	dlg->SetTitle(L"Choose the Diablo II Resurrected folder (with D2R.exe)");
	if (SUCCEEDED(dlg->Show(g.wnd))) {
		IShellItem* item = nullptr;
		if (SUCCEEDED(dlg->GetResult(&item))) {
			PWSTR path = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
				if (!LooksLikeD2R(path)) {
					MessageBoxW(g.wnd, L"That folder has no D2R.exe and Data folder.", L"Median XL", MB_ICONWARNING);
				} else {
					g.opt.source = path;
					g.launcher = std::make_unique<Launcher>(g.opt);
					RefreshStatus();
				}
				CoTaskMemFree(path);
			}
			item->Release();
		}
	}
	dlg->Release();
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM lp) {
	switch (m) {
	case WM_COMMAND:
		switch (LOWORD(w)) {
		case IdPlay: case IdUpdate: case IdRepair: case IdUninstall: RunAction(LOWORD(w)); return 0;
		case IdSaves: {
			std::wstring d = g.launcher->SavesDir();
			MakeDirs(d);
			ShellExecuteW(h, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			return 0;
		}
		case IdIssues: ShellExecuteW(h, L"open", Wide(IssuesUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
		case IdChoose: if (!g.busy) ChooseSource(); return 0;
		case IdCancel: g.launcher->cancel = true; return 0;
		}
		break;
	case WmProgress: case WmDone: {
		std::unique_ptr<ProgressMsg> msg(reinterpret_cast<ProgressMsg*>(lp));
		SetWindowTextW(g.line, Wide(msg->text).c_str());
		if (msg->fraction >= 0) {
			SendMessageW(g.bar, PBM_SETMARQUEE, FALSE, 0);
			SetWindowLongPtrW(g.bar, GWL_STYLE, GetWindowLongPtrW(g.bar, GWL_STYLE) & ~PBS_MARQUEE);
			SendMessageW(g.bar, PBM_SETPOS, WPARAM(msg->fraction * 1000), 0);
		} else if (m == WmProgress) {
			SetWindowLongPtrW(g.bar, GWL_STYLE, GetWindowLongPtrW(g.bar, GWL_STYLE) | PBS_MARQUEE);
			SendMessageW(g.bar, PBM_SETMARQUEE, TRUE, 30);
		}
		if (m == WmDone) {
			SendMessageW(g.bar, PBM_SETMARQUEE, FALSE, 0);
			SetBusy(false);
			RefreshStatus();
			if (!w)
				MessageBoxW(h, Wide(msg->text).c_str(), L"Median XL", MB_ICONWARNING);
		}
		return 0;
	}
	case WM_CTLCOLORSTATIC:
		SetBkMode(HDC(w), TRANSPARENT);
		return LRESULT(GetSysColorBrush(COLOR_WINDOW));
	case WM_CLOSE:
		if (g.busy && MessageBoxW(h, L"An installation is running. Stop it and quit? It continues where it stopped next time.", L"Median XL",
		                          MB_YESNO | MB_ICONQUESTION) != IDYES)
			return 0;
		g.launcher->cancel = true;
		if (g.worker.joinable())
			g.worker.join();
		DestroyWindow(h);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(h, m, w, lp);
}

int RunUi(HINSTANCE inst) {
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&icc);
	SetupLog(g.opt, false);
	Log(std::string("Median XL launcher ") + LauncherVersion + " (window)");

	WNDCLASSEXW wc{sizeof(wc)};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
	wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
	wc.lpszClassName = L"MedianXLLauncher";
	RegisterClassExW(&wc);

	UINT dpi = GetDpiForSystem();
	auto S = [dpi](int v) { return MulDiv(v, int(dpi), 96); };
	RECT r{0, 0, S(620), S(300)};
	AdjustWindowRectEx(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
	std::wstring title = L"Median XL Launcher " + Wide(LauncherVersion);
	g.wnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
	                        CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
	NONCLIENTMETRICSW ncm{sizeof(ncm)};
	SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
	g.font = CreateFontIndirectW(&ncm.lfMessageFont);
	auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int hgt, int id) {
		HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(hgt), g.wnd, HMENU(INT_PTR(id)), inst, nullptr);
		SendMessageW(c, WM_SETFONT, WPARAM(g.font), TRUE);
		return c;
	};
	g.status = mk(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 16, 12, 588, 88, 0);
	g.line = mk(L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 16, 108, 588, 36, 0);
	g.bar = mk(PROGRESS_CLASSW, L"", 0, 16, 148, 588, 16, 0);
	SendMessageW(g.bar, PBM_SETRANGE32, 0, 1000);
	const wchar_t* labels[] = {L"&Play", L"&Update", L"&Repair", L"Uninstall", L"Open saves folder", L"Report a problem", L"Choose D2R folder", L"Stop"};
	int ids[] = {IdPlay, IdUpdate, IdRepair, IdUninstall, IdSaves, IdIssues, IdChoose, IdCancel};
	for (int i = 0; i < 4; ++i)
		g.buttons[i] = mk(L"BUTTON", labels[i], i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, 16 + i * 148, 180, 140, 36, ids[i]);
	for (int i = 4; i < 7; ++i)
		g.buttons[i] = mk(L"BUTTON", labels[i], BS_PUSHBUTTON, 16 + (i - 4) * 148, 228, 140, 28, ids[i]);
	g.buttons[7] = mk(L"BUTTON", labels[7], BS_PUSHBUTTON, 16 + 3 * 148, 228, 140, 28, ids[7]);
	ShowWindow(g.buttons[7], SW_HIDE);
	mk(L"STATIC", L"Median XL by the Median XL team; Diablo II by Blizzard Entertainment. Not affiliated with Blizzard.", SS_LEFT, 16, 270, 588, 20, 0);

	g.launcher = std::make_unique<Launcher>(g.opt);
	g.launcher->onProgress = [](const Progress& p) {
		PostMessageW(g.wnd, WmProgress, 0, reinterpret_cast<LPARAM>(new ProgressMsg{p.text, p.fraction}));
	};
	ShowWindow(g.wnd, SW_SHOWNORMAL);
	SetWindowTextW(g.line, L"Checking for updates ...");
	RefreshStatus();
	SetBusy(true);
	g.worker = std::thread([] {  // a feed check at start so the status shows "update available"
		std::string err;
		bool ok = g.launcher->CheckFeed(err);
		PostMessageW(g.wnd, WmDone, 1, reinterpret_cast<LPARAM>(new ProgressMsg{ok ? "Ready." : err, -2}));
	});
	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		if (!IsDialogMessageW(g.wnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	if (g.worker.joinable())
		g.worker.join();
	return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
	SetUserAgent(L"MedianXL-Launcher/" + Wide(LauncherVersion));
	Cli c = ParseArgs();
	if (c.bad) {
		MessageBoxW(nullptr, Usage, L"Median XL launcher", MB_ICONINFORMATION);
		return 2;
	}
	if (!c.command.empty())
		return RunCli(c);
	g.opt = c.opt;
	return RunUi(inst);
}
