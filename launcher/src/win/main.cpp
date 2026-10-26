// Median XL launcher: the window (window.cpp: status, progress, Play / Update / Repair / Uninstall, saves folder, bug
// reports) or, with --cli, the same actions from a terminal (used by the tests, docs/launcher.md "Command line").
#include "app.h"
#include "game.h"
#include "http.h"
#include "util.h"
#include "window.h"

#include <windows.h>
#include <shellapi.h>


using namespace mxl;

namespace {

const wchar_t* Usage =
	L"MedianXL-Launcher.exe [--cli status|detect|install|update|repair|play|uninstall|self-update] [options]\n"
	L"  --data-dir DIR   state, cache and log (default %LOCALAPPDATA%\\MedianXL)\n"
	L"  --site URL|DIR   download site root (default https://dl.werdegames.com/mxl/; http://, file:// or a folder for tests)\n"
	L"  --channel NAME   release channel: stable (default) or test\n"
	L"  --source DIR     the D2R folder to copy (default: found via Battle.net / Steam)\n"
	L"  --game-dir DIR   the Median XL game copy (default: on the D2R drive)\n"
#ifdef MXL_LAUNCHER_TEST_OPTIONS
	L"  --any-drive      test builds only: allow a game copy on another drive than D2R\n"
#endif
	L"  --args \"...\"     more game arguments, e.g. -w\n"
	L"  --background     start the game without taking the foreground (tests)\n"
	L"  --loader-url URL another source of the pinned D2RLoader zip (its sha256 is still checked)\n";

struct Cli {
	std::wstring command;
	std::wstring snapshot;
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
		else if (a == L"--site") c.opt.site = Utf8(next());
		else if (a == L"--channel") c.opt.channel = Utf8(next());
		else if (a == L"--source") c.opt.source = next();
		else if (a == L"--game-dir") c.opt.gameDir = next();
#ifdef MXL_LAUNCHER_TEST_OPTIONS
		else if (a == L"--any-drive") c.opt.anyDrive = true;  // never in a player build (cmake -DMXL_LAUNCHER_TEST_OPTIONS=ON)
#endif
		else if (a == L"--args") c.opt.extraArgs = next();
		else if (a == L"--background") c.opt.background = true;
		else if (a == L"--loader-url") c.opt.loaderUrl = Utf8(next());
		else if (a == L"--snapshot") c.snapshot = next();
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
	else if (c.command == L"self-update") {
		std::string feedErr;
		if (!l.CheckFeed(feedErr) && !l.LauncherUpdateAvailable())
			Log("feed: " + feedErr);
		ok = l.SelfUpdate(err);
	}
	else {
		Log(Utf8(Usage));
		return 2;
	}
	Log(ok ? "OK" : "FAILED: " + err);
	return ok ? 0 : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
	SetUserAgent(L"MedianXL-Launcher/" + Wide(LauncherVersion));
	Launcher::RemoveOldExe();
	Cli c = ParseArgs();
	if (c.bad) {
		MessageBoxW(nullptr, Usage, L"Median XL launcher", MB_ICONINFORMATION);
		return 2;
	}
	if (!c.command.empty())
		return RunCli(c);
	SetupLog(c.opt, false);
	Log(std::string("Median XL launcher ") + LauncherVersion + " (window)");
	return RunWindow(inst, c.opt, c.snapshot);
}
