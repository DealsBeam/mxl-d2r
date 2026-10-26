// Unit tests of the launcher's portable core (no Windows headers): manifest parsing and the install plan, path
// safety, D2R detection parsers and the build check, zip directory + inflate, TOML edits, the copy filter.
// Built and run by CI on Linux (g++) and locally with MSVC: cmake -S launcher/tests -B build/launcher-tests.
#include "../src/core/detect.h"
#include "../src/core/gamecopy.h"
#include "../src/core/inflate.h"
#include "../src/core/json.h"
#include "../src/core/manifest.h"
#include "../src/core/sha256.h"
#include "../src/core/zip.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
int failures = 0, checks = 0;
void Check(bool ok, const char* what) {
	++checks;
	if (!ok) {
		++failures;
		std::printf("FAIL: %s\n", what);
	}
}

const std::string H1(64, 'a'), H2(64, 'b'), H3(64, 'c'), H4(64, 'd');

std::string ManifestText(const std::string& files, const std::string& extra = "") {
	return R"({"schemaVersion": 1, "name": "MedianXL", "version": "0.70.0", "pluginVersion": "0.70.0", "complete": true,
	  "d2r": {"build": "93847", "version": "3.3.0", "imageTimestamp": "0x6ab3782c"},
	  "d2rloader": {"version": "1.3.1-beta", "url": "https://d2rloader.net/download/D2RLoader-1.3.1-beta.zip", "size": 47652455,
	                "sha256": "9286C6B5BFF7F1043658411DBEA305FAA9698B6A455E6441668FC6D02C22AAD7", "signerThumbprint": "8001082E968FADAA58BF2F73EFA1810A11D0C28F"},
	  "launch": {"default_mod": "MedianXL", "launch_arguments": "-txt"}, "modFolder": "MedianXL", "saveFolder": "MedianXL/",
	  "zip": {"name": "MedianXL-D2R-0.70.0.zip", "size": 100, "sha256": ")" + H1 + R"("}, "someFutureField": {"x": [1]},
	  "files": [)" + files + "]" + extra + "}";
}
std::string F(const std::string& path, const std::string& sha, int size = 10) {
	return R"({"path": ")" + path + R"(", "size": )" + std::to_string(size) + R"(, "sha256": ")" + sha + R"("})";
}

void TestSha256() {
	Check(mxl::Sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 of nothing");
	Check(mxl::Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
	std::string m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	Check(mxl::Sha256Hex(m.data(), m.size()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 two blocks");
	std::string a(1000000, 'a');
	mxl::Sha256 s;
	for (size_t i = 0; i < a.size(); i += 333)  // odd chunk sizes across block borders
		s.Update(a.data() + i, std::min<size_t>(333, a.size() - i));
	Check(s.HexDigest() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256 million a, chunked");
}

void TestJson() {
	mxl::Json j;
	std::string err;
	Check(mxl::Json::Parse(R"({"a": [1, 2.5, "x\u00e9\n"], "b": {"c": true, "d": null}, "e": -3})", j, err), "json parses");
	Check(j["a"].size() == 3 && j["a"][1].Num() == 2.5 && j["a"][2].Str() == "x\xc3\xa9\n", "json array and \\u escape");
	Check(j["b"]["c"].Bool() && j["b"]["d"].IsNull() && j["e"].Int() == -3, "json nested");
	Check(j["missing"]["deeper"].Str("fb") == "fb", "missing keys give the fallback");
	Check(!mxl::Json::Parse("{\"a\": 1,}", j, err), "trailing comma refused");
	Check(!mxl::Json::Parse("{\"a\": 1} x", j, err), "trailing data refused");
	mxl::Json o = mxl::Json::Object();
	o.Set("path", "a\"b\\c").Set("n", uint64_t(5000000000ull));
	mxl::Json back;
	Check(mxl::Json::Parse(o.Dump(), back, err) && back["path"].Str() == "a\"b\\c" && back["n"].Int() == 5000000000ll,
	      "json round trip with escapes and a large size");
}

void TestSafePath() {
	Check(mxl::SafeRelativePath("MedianXL.mpq/data/global/excel/skills.txt"), "normal path");
	Check(!mxl::SafeRelativePath("../x"), "parent refused");
	Check(!mxl::SafeRelativePath("a/../../x"), "inner parent refused");
	Check(!mxl::SafeRelativePath("/abs"), "absolute refused");
	Check(!mxl::SafeRelativePath("C:/x"), "drive refused");
	Check(!mxl::SafeRelativePath("a\\b"), "backslash refused");
	Check(!mxl::SafeRelativePath("a//b"), "empty segment refused");
	Check(!mxl::SafeRelativePath("a/./b"), "dot segment refused");
	Check(!mxl::SafeRelativePath("a/nul.txt"), "device name refused");
	Check(!mxl::SafeRelativePath("a/b "), "trailing space refused");
	Check(!mxl::SafeRelativePath("a/b?"), "wildcard refused");
	Check(!mxl::SafeRelativePath(""), "empty refused");
}

void TestManifest() {
	mxl::Manifest m;
	std::string err;
	Check(mxl::ParseManifest(ManifestText(F("a.txt", H1) + "," + F("d2rloader/config/d2rloader.toml", H2),
	                                      R"(, "preserve": ["d2rloader/config/d2rloader.toml"], "fileCount": 2, "totalSize": 20)"), m, err),
	      "manifest parses (release.md schemaVersion 1, unknown fields ignored)");
	Check(m.files.size() == 2 && m.d2rBuild == 93847 && m.modFolder == "MedianXL" && m.preserve.count("d2rloader/config/d2rloader.toml") &&
	          m.complete && m.imageTimestamp == 0x6ab3782cu && m.launchArguments == "-txt" && m.saveFolder == "MedianXL/" &&
	          m.zipName == "MedianXL-D2R-0.70.0.zip" && m.d2rloader.size == 47652455 &&
	          m.d2rloader.sha256 == "9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7",
	      "manifest fields");
	Check(!mxl::ParseManifest(ManifestText(F("a.txt", H1), R"(, "fileCount": 3)"), m, err), "fileCount mismatch refused");
	Check(!mxl::ParseManifest(ManifestText(F("a.txt", H1), R"(, "totalSize": 11)"), m, err), "totalSize mismatch refused");
	Check(!mxl::ParseManifest(ManifestText(F("../evil.dll", H1)), m, err), "manifest with an unsafe path refused");
	Check(!mxl::ParseManifest(ManifestText(F("a.txt", "xyz")), m, err), "manifest with a bad sha refused");
	Check(!mxl::ParseManifest(ManifestText(F("a.txt", H1) + "," + F("A.TXT", H2)), m, err), "case-insensitive duplicate refused");
	Check(!mxl::ParseManifest(ManifestText(""), m, err), "empty file list refused");
	Check(!mxl::ParseManifest(R"({"schemaVersion": 2})", m, err), "unknown schemaVersion refused");
	Check(!mxl::ParseManifest(R"({"schema": 1})", m, err), "old draft format refused");
	mxl::Manifest back;
	mxl::ParseManifest(ManifestText(F("a.txt", H1), R"(, "preserve": ["a.txt"])"), m, err);
	Check(mxl::ParseManifest(mxl::ManifestToJson(m).Dump(), back, err) && back.files.size() == 1 && back.files[0].sha256 == H1 &&
	          back.imageTimestamp == m.imageTimestamp && back.preserve.count("a.txt") && back.d2rBuild == 93847 && back.complete,
	      "manifest round trip (state file)");
}

void TestHosting() {
	mxl::Manifest m;
	std::string err;
	std::string pub = R"(, "objectsBase": "objects/", "launcher": {"version": "0.2.0", "path": "launcher/0.2.0/MedianXL-Launcher.exe", "size": 5, "sha256": ")" + H3 + R"("})";
	std::string text = ManifestText(F("a.txt", H1) + "," + F("b/copy.txt", H1) + "," + F("c.txt", H2), pub);
	text.replace(text.find(R"("name": "MedianXL-D2R)"), 0, R"("path": "releases/0.70.0/MedianXL-D2R-0.70.0.zip", )");
	Check(mxl::ParseManifest(text, m, err), "published manifest parses");
	Check(m.objectsBase == "objects/" && m.zipPath == "releases/0.70.0/MedianXL-D2R-0.70.0.zip" && m.launcher.version == "0.2.0" &&
	          m.launcher.sha256 == H3 && m.launcher.size == 5,
	      "objectsBase, zip.path, launcher");
	mxl::Manifest back;
	Check(mxl::ParseManifest(mxl::ManifestToJson(m).Dump(), back, err) && back.objectsBase == "objects/" && back.zipPath == m.zipPath &&
	          back.launcher.path == m.launcher.path,
	      "published fields round trip");
	Check(!mxl::ParseManifest(ManifestText(F("a.txt", H1), R"(, "objectsBase": "../x/")"), m, err), "unsafe objectsBase refused");
	Check(!mxl::ParseManifest(ManifestText(F("a.txt", H1), R"(, "objectsBase": "objects")"), m, err), "objectsBase without '/' refused");
	Check(!mxl::ParseManifest(ManifestText(F("a.txt", H1), R"(, "objectsBase": "https://evil/")"), m, err), "absolute objectsBase refused");
	Check(mxl::ObjectPath("objects/", H2) == "objects/bb/" + H2, "object path");
	auto groups = mxl::GroupObjects({{"a.txt", 10, H1}, {"b/copy.txt", 10, H1}, {"c.txt", 7, H2}});
	Check(groups.size() == 2 && groups[0].sha256 == H1 && groups[0].paths.size() == 2 && groups[0].paths[1] == "b/copy.txt" &&
	          groups[1].size == 7 && groups[1].paths.size() == 1,
	      "same content is one download");

	mxl::Channel c;
	Check(mxl::ParseChannel(R"({"schemaVersion": 1, "version": "0.70.0", "manifest": "releases/0.70.0/release-manifest.json",
	    "manifestSha256": ")" + std::string(64, 'E') + R"(", "publishedAt": "2026-10-26T12:00:00Z", "later": 1})", c, err) &&
	          c.version == "0.70.0" && c.manifest == "releases/0.70.0/release-manifest.json" && c.manifestSha256 == std::string(64, 'e'),
	      "channel parses");
	Check(!mxl::ParseChannel(R"({"schemaVersion": 2, "version": "1", "manifest": "m.json", "manifestSha256": ")" + H1 + R"("})", c, err),
	      "unknown channel schemaVersion refused");
	Check(!mxl::ParseChannel(R"({"schemaVersion": 1, "version": "1", "manifest": "../m.json", "manifestSha256": ")" + H1 + R"("})", c, err),
	      "unsafe channel manifest path refused");
	Check(!mxl::ParseChannel(R"({"schemaVersion": 1, "version": "1", "manifest": "m.json"})", c, err), "channel without sha refused");
	Check(!mxl::ParseChannel("<html>sign in</html>", c, err), "a login page is not a channel");

	Check(mxl::SiteRoot("https://dl.werdegames.com/mxl") == "https://dl.werdegames.com/mxl/", "site root gets a '/'");
	Check(mxl::SiteRoot("http://127.0.0.1:8765/") == "http://127.0.0.1:8765/", "site root kept");
	const std::string bs(1, char(92));
	Check(mxl::SiteRoot("D:" + bs + "test site" + bs) == "file:///D:/test%20site/", "local folder becomes a file URL");
	Check(mxl::SiteRoot(bs + bs + "srv" + bs + "mxl") == "file://srv/mxl/", "UNC share becomes a file URL");
	Check(mxl::ResolveUrl(mxl::SiteRoot("http://h/mxl"), "objects/aa/x") == "http://h/mxl/objects/aa/x", "object URL under the root");
}

void TestPlan() {
	std::string err;
	mxl::Manifest next;
	mxl::ParseManifest(ManifestText(F("same.txt", H1) + "," + F("changed.txt", H2, 20) + "," + F("new.txt", H3, 30) + "," +
	                                F("d2rloader/config/d2rloader.toml", H2) + "," + F("missing.txt", H1, 5),
	                                R"(, "preserve": ["d2rloader/config/d2rloader.toml"])"),
	                   next, err);
	std::map<std::string, std::string> disk = {{"same.txt", H1}, {"changed.txt", H1}, {"gone.txt", H1},
	                                           {"d2rloader/config/d2rloader.toml", H4}, {"d2rloader/logs/x.log", H1},
	                                           {"d2rloader/crashes/c.dmp", H1}, {"stray/own.txt", H1}};
	auto cur = [&](const std::string& p) { auto it = disk.find(p); return it == disk.end() ? std::string() : it->second; };
	auto local = [&] { std::vector<std::string> v; for (const auto& [k, _] : disk) v.push_back(k); return v; };
	mxl::InstallPlan plan = mxl::PlanInstall(next, local(), cur);
	auto has = [](const std::vector<mxl::ManifestFile>& v, const char* p) {
		for (const auto& f : v) if (f.path == p) return true;
		return false;
	};
	auto hasS = [](const std::vector<std::string>& v, const char* p) {
		for (const auto& f : v) if (f == p) return true;
		return false;
	};
	Check(!has(plan.fetch, "same.txt"), "unchanged file is not fetched");
	Check(has(plan.fetch, "changed.txt") && has(plan.fetch, "new.txt") && has(plan.fetch, "missing.txt"), "changed, new, missing fetched");
	Check(hasS(plan.preserved, "d2rloader/config/d2rloader.toml") && !has(plan.fetch, "d2rloader/config/d2rloader.toml"),
	      "existing preserve file never overwritten");
	Check(hasS(plan.remove, "gone.txt") && hasS(plan.remove, "stray/own.txt") && plan.remove.size() == 2,
	      "unlisted files removed, D2RLoader logs / crashes kept");
	Check(plan.fetchBytes == 20 + 30 + 5, "fetch byte count");
	disk.clear();
	plan = mxl::PlanInstall(next, local(), cur);
	Check(plan.fetch.size() == next.files.size() && plan.remove.empty(), "fresh install fetches all, preserve file included");
	for (const auto& f : next.files) disk[f.path] = f.sha256;
	plan = mxl::PlanInstall(next, local(), cur);
	Check(plan.fetch.empty() && plan.remove.empty() && plan.preserved.empty(), "verified install: nothing to do");
}

void TestVersions() {
	Check(mxl::CompareVersions("1.2.10", "1.2.9") > 0, "1.2.10 > 1.2.9");
	Check(mxl::CompareVersions("v0.70.0", "0.70.0") == 0, "leading v");
	Check(mxl::CompareVersions("1.3.1", "1.3.1-beta") > 0, "release > beta");
	Check(mxl::CompareVersions("1.3", "1.3.0") == 0, "missing parts are 0");
	Check(mxl::ResolveUrl("https://github.com/o/r/releases/latest/download/medianxl-manifest.json", "MedianXL-0.70.0.zip") ==
	          "https://github.com/o/r/releases/latest/download/MedianXL-0.70.0.zip", "relative url");
	Check(mxl::ResolveUrl("file:///C:/feed/manifest.json", "x.zip") == "file:///C:/feed/x.zip", "relative file url");
	Check(mxl::ResolveUrl("http://127.0.0.1:8000/a/m.json", "/b/x.zip") == "http://127.0.0.1:8000/b/x.zip", "host-relative url");
	Check(mxl::ResolveUrl("https://a/m.json", "https://b/x.zip") == "https://b/x.zip", "absolute url");
}

void TestDetect() {
	std::string info =
		"Branch!STRING:0|Active!DEC:1|Build Key!HEX:16|Version!STRING:0|Product!STRING:0\r\n"
		"eu|0|aaaa|3.3.90000|osi\r\n"
		"us|1|623f|3.3.93847|osi\r\n";
	Check(mxl::BuildInfoVersion(info) == "3.3.93847", ".build.info active row");
	Check(mxl::BuildInfoVersion("Version!STRING:0\n") == "", ".build.info without rows");
	Check(mxl::BuildInfoVersion("garbage") == "", "no Version column");
	std::string real =
		"Branch!STRING:0|Active!DEC:1|Build Key!HEX:16|CDN Key!HEX:16|Install Key!HEX:16|IM Size!DEC:4|CDN Path!STRING:0|CDN Hosts!STRING:0|"
		"CDN Servers!STRING:0|Tags!STRING:0|Armadillo!STRING:0|Last Activated!STRING:0|Version!STRING:0|KeyRing!HEX:16|Product!STRING:0\n"
		"us|1|623f7a1f73eabb08ccb2b2046e3f9164|a10f39cd385730caa4ab1909c888a845|||tpr/osi|level3.blizzard.com us.cdn.blizzard.com|"
		"http://level3.blizzard.com/?maxhosts=5|Windows US? acct-USA? geoip-JP? enUS speech?:Windows US? acct-USA? geoip-JP? enUS text?|||3.3.93847||\n";
	Check(mxl::BuildInfoVersion(real) == "3.3.93847", "real .build.info of build 93847");
	Check(mxl::BuildNumber("3.3.93847") == 93847 && mxl::BuildNumber("1.0.0.93847") == 93847 && mxl::BuildNumber("") == 0, "build numbers");

	auto ok = mxl::CheckBuild("3.3.93847", "3.3.93847");
	Check(ok.verdict == mxl::BuildVerdict::Ok, "93847 is ok");
	auto newer = mxl::CheckBuild("3.3.94100", "3.3.93847");
	Check(newer.verdict == mxl::BuildVerdict::Newer && newer.build == 94100 && newer.message.find("Waiting for a Median XL update") != std::string::npos,
	      "newer build: waiting message (half-updated install counts as newer)");
	Check(mxl::CheckBuild("3.2.91000", "").verdict == mxl::BuildVerdict::Older, "older build");
	Check(mxl::CheckBuild("", "").verdict == mxl::BuildVerdict::Unknown, "unknown build");
	Check(mxl::CheckBuild("", "3.3.93847").verdict == mxl::BuildVerdict::Ok, "exe version alone");

	std::string vdf = R"("libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"label"		""
		"apps" { "228980" "340606523" }
	}
	"1"
	{
		"path"		"E:\\SteamLibrary"   // a second library
	}
})";
	auto libs = mxl::SteamLibraryPaths(vdf);
	Check(libs.size() == 2 && libs[0] == "C:\\Program Files (x86)\\Steam" && libs[1] == "E:\\SteamLibrary", "steam library paths");
	std::string acf = R"("AppState"
{
	"appid"		"2536520"
	"name"		"Diablo II: Resurrected"
	"installdir"		"Diablo II Resurrected"
	"UserConfig" { "name" "other" }
})";
	Check(mxl::AcfValue(acf, "installdir") == "Diablo II Resurrected" && mxl::AcfValue(acf, "name") == "Diablo II: Resurrected",
	      "acf top-level values (not the nested name)");
	Check(mxl::AcfIsD2R(acf), "acf names D2R");
	Check(!mxl::AcfIsD2R(R"("AppState" { "name" "Diablo IV" "installdir" "Diablo IV" })"), "Diablo IV is not D2R");
}

// A zip built in memory: one stored and one deflated (fixed Huffman, hand-made) entry.
void TestZip() {
	// "hello hello hello" deflated with fixed codes by zlib (raw)
	const uint8_t deflated[] = {0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x90, 0x00};
	std::vector<uint8_t> out;
	Check(mxl::Inflate(deflated, sizeof(deflated), out, 17) && std::string(out.begin(), out.end()) == "hello hello hello",
	      "inflate fixed-Huffman block with a back reference");
	Check(!mxl::Inflate(deflated, sizeof(deflated), out, 16), "inflate refuses a wrong size");
	// 600 bytes of shuffled class names, one dynamic-Huffman block (zlib level 9, raw)
	const uint8_t dyn[] = {0xed, 0xcc, 0x81, 0x09, 0xc3, 0x30, 0x0c, 0x04, 0xc0, 0x55, 0x7e, 0xb5, 0xb7, 0xad, 0x06, 0x41, 0x2c, 0x95, 0xff, 0x84, 0x42, 0xa7, 0xcf, 0x20, 0xed, 0x0d, 0x70, 0x64, 0xe9, 0x96, 0x32, 0xf2, 0xa3, 0x15, 0x5f, 0xb2, 0x40, 0xda, 0x44, 0x35, 0x90, 0xd4, 0x8c, 0x35, 0xdd, 0x41, 0xd1, 0x83, 0x6b, 0xbf, 0x11, 0x5b, 0xf1, 0x72, 0x79, 0xac, 0x98, 0x14, 0xae, 0x11, 0xd7, 0x36, 0x32, 0x1b, 0xa5, 0x23, 0x00, 0xb7, 0x85, 0xaa, 0xb3, 0x8d, 0xbb, 0xc4, 0x7f, 0xff, 0x9b, 0xfd, 0x03};
	const char* w = "amazon assassin barbarian druid necromancer paladin sorceress sigma tower uber ennead fortress ";
	std::string want;
	for (size_t i = 0; i < 600; ++i)
		want += w[(i * 7) % std::strlen(w)];
	Check(mxl::Inflate(dyn, sizeof(dyn), out, 600) && std::string(out.begin(), out.end()) == want, "inflate dynamic-Huffman block");
	Check(mxl::Crc32(want.data(), want.size()) == 0xb54563acu, "crc32 of the dynamic block text");
	const uint8_t stored[] = {0x01, 0x03, 0x00, 0xfc, 0xff, 'a', 'b', 'c'};
	Check(mxl::Inflate(stored, sizeof(stored), out, 3) && std::string(out.begin(), out.end()) == "abc", "inflate stored block");
	const uint8_t junk[] = {0xff, 0xff, 0xff};
	Check(!mxl::Inflate(junk, sizeof(junk), out, 3), "inflate refuses junk");
	Check(mxl::Crc32("123456789", 9) == 0xcbf43926u, "crc32 check value");

	// zip: entry "a.txt" stored "abc", entry "d/" dir; central directory; end record
	std::vector<uint8_t> z;
	auto u16 = [&](unsigned v) { z.push_back(uint8_t(v)); z.push_back(uint8_t(v >> 8)); };
	auto u32 = [&](uint32_t v) { u16(v & 0xffff); u16(v >> 16); };
	auto str = [&](const char* s) { z.insert(z.end(), s, s + std::strlen(s)); };
	uint32_t crc = mxl::Crc32("abc", 3);
	u32(0x04034b50); u16(20); u16(0); u16(0); u16(0); u16(0); u32(crc); u32(3); u32(3); u16(5); u16(0); str("a.txt"); str("abc");
	uint32_t off2 = uint32_t(z.size());
	u32(0x04034b50); u16(20); u16(0); u16(0); u16(0); u16(0); u32(0); u32(0); u32(0); u16(2); u16(0); str("d/");
	uint32_t cd = uint32_t(z.size());
	u32(0x02014b50); u16(20); u16(20); u16(0); u16(0); u16(0); u16(0); u32(crc); u32(3); u32(3); u16(5); u16(0); u16(0); u16(0); u16(0); u32(0); u32(0); str("a.txt");
	u32(0x02014b50); u16(20); u16(20); u16(0); u16(0); u16(0); u16(0); u32(0); u32(0); u32(0); u16(2); u16(0); u16(0); u16(0); u16(0); u32(0x10); u32(off2); str("d/");
	uint32_t cdSize = uint32_t(z.size()) - cd;
	u32(0x06054b50); u16(0); u16(0); u16(2); u16(2); u32(cdSize); u32(cd); u16(0);
	mxl::ZipTail tail;
	std::string err;
	Check(mxl::FindZipTail(z, z.size(), tail, err) && tail.entries == 2 && tail.cdOffset == cd && tail.cdSize == cdSize, "zip end record");
	std::vector<mxl::ZipEntry> entries;
	Check(mxl::ParseCentralDirectory(z.data() + cd, cdSize, 2, entries, err) && entries.size() == 2 && entries[0].name == "a.txt" &&
	          entries[0].size == 3 && !entries[0].dir && entries[1].dir && entries[1].offset == off2,
	      "zip central directory");
	Check(mxl::LocalHeaderSize(z.data(), z.size()) == 35, "local header size");
	std::vector<uint8_t> notZip(30, 0);
	Check(!mxl::FindZipTail(notZip, notZip.size(), tail, err), "no end record in junk");

	// ranges: entries at 0,100,200,1000,5000; cd at 6000
	std::vector<mxl::ZipEntry> all(5);
	uint64_t offs[] = {0, 100, 200, 1000, 5000};
	for (int i = 0; i < 5; ++i) all[size_t(i)].offset = offs[i];
	auto r = mxl::PlanRanges(all, {0, 1, 3, 4}, 6000, 0);
	Check(r.size() == 2 && r[0].begin == 0 && r[0].end == 200 && r[0].entries.size() == 2 && r[1].begin == 1000 && r[1].end == 6000 &&
	          r[1].entries.size() == 2,
	      "ranges: neighbours merged, a skipped entry splits");
	r = mxl::PlanRanges(all, {0, 3}, 6000, 1000);
	Check(r.size() == 1 && r[0].begin == 0 && r[0].end == 5000, "ranges: small gaps merged");
}

void TestCopyFilter() {
	Check(!mxl::ExcludedFromCopy("D2R.exe") && !mxl::ExcludedFromCopy("Data\\data\\data.024") && !mxl::ExcludedFromCopy(".build.info"),
	      "game files are copied");
	Check(mxl::ExcludedFromCopy("mods\\RMD-MP\\x.json") && mxl::ExcludedFromCopy("Mods"), "other mods stay out");
	Check(mxl::ExcludedFromCopy("D2RLoader.exe") && mxl::ExcludedFromCopy("D2RLoader-s2.exe") && mxl::ExcludedFromCopy("D2RCore.dll") &&
	          mxl::ExcludedFromCopy("d2rloader.mpq") && mxl::ExcludedFromCopy("d2rloader\\config\\d2rloader.toml"),
	      "every D2RLoader file stays out");
	Check(mxl::ExcludedFromCopy("blz-log.txt") && mxl::ExcludedFromCopy("mxl-itempkt.log") && mxl::ExcludedFromCopy("x.mxlpart") &&
	          mxl::ExcludedFromCopy("Data\\data\\data.000.mxlpart"),
	      "logs and partial copies stay out");
	Check(mxl::ExcludedFromCopy("Data/hd/objects/dummy/d2rloader_stash_proxy.json"), "D2RLoader's stash proxy stays out");
	Check(!mxl::ExcludedFromCopy("Data\\hd\\objects\\dummy\\other.json") && !mxl::ExcludedFromCopy("D2R_loader.dll"),
	      "Blizzard's own D2R_loader.dll is copied");
}

void TestToml() {
	std::string t = "# head\n[d2rloader]\n# Launch this mod automatically.\ndefault_mod = \"\"  # comment\ncheck_for_updates = true\n\n[d2rloader.backups]\nenabled = true\n";
	std::string u = mxl::SetTomlKey(t, "d2rloader", "default_mod", "\"MedianXL\"");
	Check(mxl::GetTomlKey(u, "d2rloader", "default_mod") == "\"MedianXL\"" && u.find("# comment") != std::string::npos, "replace keeps the comment");
	u = mxl::SetTomlKey(u, "d2rloader", "check_for_updates", "false");
	Check(mxl::GetTomlKey(u, "d2rloader", "check_for_updates") == "false", "replace a bool");
	Check(mxl::GetTomlKey(u, "d2rloader.backups", "enabled") == "true", "other section untouched");
	u = mxl::SetTomlKey(u, "d2rloader", "skip_title_screen", "true");
	Check(mxl::GetTomlKey(u, "d2rloader", "skip_title_screen") == "true" && mxl::GetTomlKey(u, "d2rloader.backups", "skip_title_screen").empty(),
	      "a missing key is added in its own section");
	u = mxl::SetTomlKey(u, "d2rcore.stash", "add_shared_tabs", "7");
	Check(mxl::GetTomlKey(u, "d2rcore.stash", "add_shared_tabs") == "7", "a missing section is appended");
	std::string c = "[a]\r\nx = 1\r\n";
	std::string cu = mxl::SetTomlKey(c, "a", "x", "2");
	Check(cu == "[a]\r\nx = 2\r\n", "CRLF kept");
	Check(mxl::SetTomlKey("[a]\nx = 1", "a", "x", "2") == "[a]\nx = 2", "no final newline stays without one");
	Check(mxl::SetTomlKey("[a]\n# x = 1\n", "a", "x", "2") == "[a]\nx = 2\n", "commented template replaced");
	Check(mxl::GetTomlKey("[a]\ns = \"a # b\" # c\n", "a", "s") == "\"a # b\"", "# inside quotes is no comment");
}

}  // namespace

int main() {
	TestSha256();
	TestJson();
	TestSafePath();
	TestManifest();
	TestHosting();
	TestPlan();
	TestVersions();
	TestDetect();
	TestZip();
	TestCopyFilter();
	TestToml();
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
