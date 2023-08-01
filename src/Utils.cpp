#include "Utils.h"

#include <cstdarg>
#include <cerrno>
#include <string.h>
#include <stdexcept>

std::vector<std::string> strSplit(const std::string &sep, const std::string &str) {
	std::vector<std::string> result;
	size_t last_pos = 0;
	
	while (true) {
		size_t pos = str.find(sep, last_pos);
		if (pos == std::string::npos) {
			result.push_back(str.substr(last_pos));
			break;
		} else {
			result.push_back(str.substr(last_pos, pos - last_pos));
			last_pos = pos + 1;
		}
	}
	
	return result;
}

int strToInt(const std::string &s, int base, int default_value) {
	try {
		return stoi(s, nullptr, base);
	} catch (std::invalid_argument &e) {
		return default_value;
	}
}

std::string ip2str(const struct sockaddr *src) {
	char buff[256];
	if (src) {
		if (src->sa_family == AF_INET) {
			const struct sockaddr_in *addr_ip4 = reinterpret_cast<const struct sockaddr_in *>(src);
			if (uv_ip4_name(addr_ip4, buff, sizeof(buff)) == 0)
				return std::string(buff) + std::string(":") + std::to_string(ntohs(addr_ip4->sin_port));
		} else if (src->sa_family == AF_INET6) {
			const struct sockaddr_in6 *addr_ip6 = reinterpret_cast<const struct sockaddr_in6 *>(src);
			if (uv_ip6_name(addr_ip6, buff, sizeof(buff)) == 0)
				return std::string(buff) + std::string(":") + std::to_string(ntohs(addr_ip6->sin6_port));
		}
	}
	return "(unknown)";
}

std::string readFile(const std::string &path) {
	FILE *fp = fopen(path.c_str(), "r");
	if (!fp)
		throw std::runtime_error(strprintf("fopen(%s) error: %s", path.c_str(), strerror(errno)));
	
	char buff[4096];
	std::string result;
	while (!feof(fp)) {
		int readed = fread(buff, 1, sizeof(buff), fp);
		if (readed > 0)
			result.append(buff, readed);
	}
	fclose(fp);
	
	return result;
}

std::string strprintf(const char *format, ...) {
	va_list v;
	
	std::string out;
	
	va_start(v, format);
	int n = vsnprintf(nullptr, 0, format, v);
	va_end(v);
	
	if (n <= 0)
		throw std::runtime_error("vsnprintf error...");
	
	out.resize(n);
	
	va_start(v, format);
	vsnprintf(&out[0], out.size() + 1, format, v);
	va_end(v);
	
	return out;
}
