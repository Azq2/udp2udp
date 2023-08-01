#pragma once

#include <string>
#include <vector>
#include <time.h>
#include <uv.h>

std::vector<std::string> strSplit(const std::string &sep, const std::string &str);
int strToInt(const std::string &s, int base = 10, int default_value = 0);
int64_t getCurrentTimestamp();
std::string ip2str(const struct sockaddr *src);

inline std::string ip2str(const struct sockaddr_in *src) {
	return ip2str(reinterpret_cast<const struct sockaddr *>(src));
}

inline std::string ip2str(const struct sockaddr_in6 *src) {
	return ip2str(reinterpret_cast<const struct sockaddr *>(src));
}

std::string readFile(const std::string &path);
std::string strprintf(const char *format, ...);
