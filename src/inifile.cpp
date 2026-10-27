#include "inifile.h"

#include <strings.h>
#include <vector>

namespace inifile {
namespace {

std::vector<std::string> Lines(const std::string &text)
{
	std::vector<std::string> lines;
	size_t start = 0;
	while (start < text.size()) {
		const size_t end = text.find('\n', start);
		lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	return lines;
}

std::string Trimmed(const std::string &s)
{
	const size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
	return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// The section a "[name]" line opens, or "".
std::string SectionOf(const std::string &line)
{
	const std::string t = Trimmed(line);
	return t.size() > 2 && t.front() == '[' && t.back() == ']' ? t.substr(1, t.size() - 2) : "";
}

bool IsKey(const std::string &line, const std::string &key)
{
	const std::string t = Trimmed(line);
	const size_t eq = t.find('=');
	return eq != std::string::npos && !strcasecmp(Trimmed(t.substr(0, eq)).c_str(), key.c_str());
}

// The first `key=` line given `value`, its comment kept; false if there's none.
bool Replace(std::vector<std::string> &lines, const std::string &key, const std::string &value)
{
	for (std::string &line : lines) {
		if (!IsKey(line, key))
			continue;
		const size_t eq = line.find('=');
		const std::string rest = line.substr(eq + 1);
		const size_t semi = rest.find(';');
		size_t end = rest.size();
		if (semi != std::string::npos) {
			const size_t last = semi > 0 ? rest.find_last_not_of(" \t", semi - 1) : std::string::npos;
			end = last == std::string::npos ? 0 : last + 1;
		}
		line = line.substr(0, eq + 1) + value + rest.substr(end);
		return true;
	}
	return false;
}

} // namespace

std::string WithValue(const std::string &ini, const std::string &section, const std::string &key,
                      const std::string &value)
{
	std::vector<std::string> lines = Lines(ini);
	if (!Replace(lines, key, value)) {
		// At the end of [section]: its last line before the next section's.
		int at = -1;
		for (size_t i = 0; i < lines.size(); i++) {
			const std::string name = SectionOf(lines[i]);
			if (!name.empty() && at >= 0)
				break;
			if (!name.empty() && !strcasecmp(name.c_str(), section.c_str()))
				at = int(i);
			else if (at >= 0)
				at = int(i);
		}
		if (at >= 0)
			lines.insert(lines.begin() + at + 1, key + "=" + value);
		else {
			lines.push_back("[" + section + "]");
			lines.push_back(key + "=" + value);
		}
	}
	std::string out;
	for (const std::string &line : lines)
		out += line + "\n";
	return out;
}

} // namespace inifile
