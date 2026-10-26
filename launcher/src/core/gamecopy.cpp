#include "gamecopy.h"

#include <cctype>
#include <cstdint>
#include <vector>

namespace mxl {
namespace {

std::string Norm(const std::string& rel) {
	std::string s;
	for (char c : rel)
		s += c == '\\' ? '/' : char(std::tolower(uint8_t(c)));
	while (!s.empty() && s[0] == '/')
		s.erase(0, 1);
	return s;
}

bool Under(const std::string& s, const char* dir) {
	std::string d = dir;
	return s == d || s.rfind(d + "/", 0) == 0;
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

bool ExcludedFromCopy(const std::string& rel) {
	std::string s = Norm(rel);
	if (s.empty())
		return false;
	if (Under(s, "mods") || Under(s, "d2rloader") || Under(s, "logs") || Under(s, "crashes"))
		return true;
	if (s.find('/') == std::string::npos) {  // top-level files
		if (s.rfind("d2rloader", 0) == 0)  // D2RLoader.exe, D2RLoader-s2.exe, d2rloader.mpq
			return true;
		if (s == "d2rcore.dll" || s == "blz-log.txt" || s == "dxdiag.txt" || s == "medianxl-copy.json")
			return true;
		size_t dot = s.rfind('.');
		std::string ext = dot == std::string::npos ? "" : s.substr(dot);
		if (ext == ".log" || ext == ".dmp" || ext == ".mxlpart")
			return true;
	}
	// D2RLoader writes this proxy into the game's data at every start; the copy's loader makes its own.
	if (s == "data/hd/objects/dummy/d2rloader_stash_proxy.json")
		return true;
	return s.size() > 8 && s.compare(s.size() - 8, 8, ".mxlpart") == 0;
}

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

}  // namespace mxl
