#include "Utils.h"

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
				return std::string(buff) + std::string(":") + std::to_string(addr_ip4->sin_port);
		} else if (src->sa_family == AF_INET6) {
			const struct sockaddr_in6 *addr_ip6 = reinterpret_cast<const struct sockaddr_in6 *>(src);
			if (uv_ip6_name(addr_ip6, buff, sizeof(buff)) == 0)
				return std::string(buff) + std::string(":") + std::to_string(addr_ip6->sin6_port);
		}
	}
	return "(unknown)";
}
