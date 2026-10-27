#include "paths.h"

#include "json.h"
#include "manifest.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mxl {
namespace {

std::string NormPath(const std::string& p) {  // lower case, '/' separators, no trailing '/'
	std::string s;
	for (char c : p)
		s += c == char(92) ? '/' : char(std::tolower(uint8_t(c)));  // char(92): backslash
	while (s.size() > 1 && s.back() == '/')
		s.pop_back();
	return s;
}

std::string Join(const std::string& a, const std::string& b) {
	if (a.empty())
		return b;
	return a.back() == char(92) || a.back() == '/' ? a + b : a + char(92) + b;
}

std::vector<std::string> Lines(const std::string& text) {
	std::vector<std::string> out;
	size_t start = 0;
	while (start < text.size()) {
		size_t p = text.find('\n', start);
		if (p == std::string::npos) {
			out.push_back(text.substr(start));
			break;
		}
		out.push_back(text.substr(start, p - start));
		start = p + 1;
	}
	return out;
}

std::string Strip(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
	return s.substr(a, b - a);
}

bool IsHeader(const std::string& line, std::string* name = nullptr) {
	std::string t = Strip(line);
	if (t.size() < 3 || t[0] != '[')
		return false;
	size_t close = t.find(']');
	if (close == std::string::npos)
		return false;
	if (name)
		*name = Strip(t.substr(1, close - 1));
	return true;
}

// "key = value # c" -> true and the value part (comment cut outside quotes)
bool KeyLine(const std::string& line, const std::string& key, std::string* value, std::string* comment) {
	std::string t = Strip(line);
	if (t.compare(0, key.size(), key) != 0)
		return false;
	std::string rest = Strip(t.substr(key.size()));
	if (rest.empty() || rest[0] != '=')
		return false;
	rest = rest.substr(1);
	char quote = 0;
	size_t cut = std::string::npos;
	for (size_t i = 0; i < rest.size(); ++i) {
		char c = rest[i];
		if (quote) {
			if (c == '\\' && quote == '"') { ++i; continue; }
			if (c == quote) quote = 0;
		} else if (c == '"' || c == '\'') {
			quote = c;
		} else if (c == '#') {
			cut = i;
			break;
		}
	}
	if (value)
		*value = Strip(cut == std::string::npos ? rest : rest.substr(0, cut));
	if (comment)
		*comment = cut == std::string::npos ? "" : " " + Strip(rest.substr(cut));
	return true;
}

}  // namespace

std::string SetTomlKey(const std::string& text, const std::string& section, const std::string& key, const std::string& value) {
	auto lines = Lines(text);
	bool crlf = text.find("\r\n") != std::string::npos;
	std::string eol = crlf ? "\r" : "";
	int start = -1, end = int(lines.size());
	for (int i = 0; i < int(lines.size()); ++i) {
		std::string name;
		if (!IsHeader(lines[size_t(i)], &name))
			continue;
		if (start >= 0) {
			end = i;
			break;
		}
		if (name == section)
			start = i;
	}
	auto join = [&](const std::vector<std::string>& ls) {
		std::string out;
		for (const auto& l : ls)
			out += l + "\n";
		if (!text.empty() && text.back() != '\n' && !out.empty())
			out.pop_back();  // keep a missing final newline missing
		return out;
	};
	if (start < 0) {
		std::string out = text;
		if (!out.empty() && out.back() != '\n')
			out += crlf ? "\r\n" : "\n";
		std::string nl = crlf ? "\r\n" : "\n";
		out += nl + "[" + section + "]" + nl + key + " = " + value + nl;
		return out;
	}
	for (int i = start + 1; i < end; ++i) {
		std::string comment;
		if (KeyLine(lines[size_t(i)], key, nullptr, &comment)) {
			std::string indent = lines[size_t(i)].substr(0, lines[size_t(i)].find_first_not_of(" \t"));
			lines[size_t(i)] = indent + key + " = " + value + comment + eol;
			return join(lines);
		}
	}
	for (int i = start + 1; i < end; ++i) {
		std::string t = Strip(lines[size_t(i)]);
		if (t.size() > 1 && t[0] == '#') {
			std::string bare = Strip(t.substr(1));
			if (KeyLine(bare, key, nullptr, nullptr)) {
				lines[size_t(i)] = key + " = " + value + eol;
				return join(lines);
			}
		}
	}
	lines.insert(lines.begin() + start + 1, key + " = " + value + eol);
	return join(lines);
}

std::string GetTomlKey(const std::string& text, const std::string& section, const std::string& key) {
	auto lines = Lines(text);
	bool in = false;
	for (const auto& l : lines) {
		std::string name;
		if (IsHeader(l, &name)) {
			in = name == section;
			continue;
		}
		std::string v;
		if (in && KeyLine(l, key, &v, nullptr))
			return v;
	}
	return "";
}

bool PathIsOrUnder(const std::string& path, const std::string& dir) {
	std::string p = NormPath(path), d = NormPath(dir);
	if (d.empty() || (p + "/").find("/../") != std::string::npos || p.rfind("../", 0) == 0)
		return false;
	return p == d || p.rfind(d + "/", 0) == 0;
}

std::string DefaultDataDir(const std::string& localAppData) { return Join(localAppData, ProductFolder); }

bool SafeModFolderName(const std::string& name) {
	if (name.empty() || name.size() > 64 || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' || name[0] == ' ')
		return false;
	for (char c : name)
		if (!(std::isalnum(uint8_t(c)) || c == '-' || c == '_' || c == '.' || c == ' '))
			return false;
	return SafeRelativePath(name);  // device names (CON, NUL, ...)
}

std::string ModTargetDir(const std::string& d2rDir, const std::string& modFolder) {
	if (d2rDir.empty() || !SafeModFolderName(modFolder))
		return "";
	std::string d = d2rDir;
	while (d.size() > 3 && (d.back() == char(92) || d.back() == '/'))
		d.pop_back();
	return Join(Join(d, "mods"), modFolder);
}

MarkerVerdict CheckMarker(const std::string& text, const std::string& stateId) {
	if (text.empty())
		return MarkerVerdict::None;
	Json j;
	std::string err;
	if (!Json::Parse(text, j, err) || !j.IsObject() || j["made_by"].Str() != MarkerMadeBy || j["id"].Str().empty())
		return MarkerVerdict::Unreadable;
	return stateId.empty() || j["id"].Str() == stateId ? MarkerVerdict::Ours : MarkerVerdict::OtherId;
}

std::string MakeMarker(const std::string& id, const std::string& version) {
	Json m = Json::Object();
	m.Set("made_by", MarkerMadeBy).Set("id", id).Set("version", version);
	return m.Dump();
}

bool LoaderVersionMatches(const std::string& found, const std::string& required) {
	auto nums = [](const std::string& v) {
		std::vector<long> n;
		size_t i = 0;
		while (i < v.size() && std::isdigit(uint8_t(v[i]))) {
			long x = 0;
			while (i < v.size() && std::isdigit(uint8_t(v[i])))
				x = x * 10 + (v[i++] - '0');
			n.push_back(x);
			if (i < v.size() && v[i] == '.')
				++i;
			else
				break;
		}
		while (n.size() > 3 && n.back() == 0)  // "1.3.1.0" is 1.3.1
			n.pop_back();
		return n;
	};
	std::vector<long> f = nums(found), r = nums(required);
	return !f.empty() && f == r;
}

bool OfficialMedianXLPath(const std::string& path, const std::string& localAppData, const std::string& roamingAppData) {
	if (PathIsOrUnder(path, Join(localAppData, "mxl-launcher")) || PathIsOrUnder(path, Join(localAppData, "MedianXL")) ||
	    PathIsOrUnder(path, Join(roamingAppData, "MedianXL")))
		return true;
	std::string p = NormPath(path) + "/";
	return p.find("/diablo ii/") != std::string::npos;  // the classic game folder, wherever it is
}

bool IsGameProcessName(const std::string& exe) {
	size_t slash = exe.find_last_of(R"(\/)");
	std::string n = exe.substr(slash == std::string::npos ? 0 : slash + 1);
	for (char& ch : n)
		ch = char(std::tolower(static_cast<unsigned char>(ch)));
	auto ends = [&](const char* tail) { size_t k = std::strlen(tail); return n.size() >= k && n.compare(n.size() - k, k, tail) == 0; };
	return n == "d2r.exe" || (n.rfind("d2rloader", 0) == 0 && ends(".exe"));
}

}  // namespace mxl
