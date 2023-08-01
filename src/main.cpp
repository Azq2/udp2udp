#include "UdpProxy.h"
#include "Utils.h"

#include <cstring>
#include <iostream>
#include <uv.h>
#include <signal.h>
#include <nlohmann/json.hpp>

/*
Config format:

[{
	"src": "LISTEN_IP:LISTEN_PORT",
	"dst": "DST_IP:DST_PORT,DST_IP2:DST_PORT2,...",
	"xor": {"key": 170, "size": 40}
}]

*/

using json = nlohmann::json;

void signal_callback_handler(int signum) {
	std::cout << "Caught signal " << signum << "\n";
	exit(signum);
}

int main(int argc, char *argv[]) {
	signal(SIGINT, signal_callback_handler);
	
	if (argc != 2) {
		std::cerr << "usage: " + std::string(argv[0]) + " /etc/udp2udp.json\n";
		return -1;
	}
	
	try {
		auto *loop = uv_default_loop();
		
		auto configs = json::parse(readFile(argv[1]));
		for (auto &config: configs) {
			std::cerr << "src: " << config["src"] << ", dst: " << config["dst"] << "\n";
			UdpProxy *proxy = new UdpProxy(config["src"].get<std::string>(), config["dst"].get<std::string>());
			
			if (config.contains("xor")) {
				int max_length = config["xor"].contains("size") ? config["xor"]["size"].get<int>() : 0;
				proxy->setXorEncryption(config["xor"]["key"].get<int>(), max_length);
			}
			
			proxy->run(loop);
		}
		
		std::cerr << "Running server...\n";
		uv_run(loop, UV_RUN_DEFAULT);
	} catch (const std::exception& ex) {
		std::cerr << "FATAL: " << ex.what() << "\n";
		return -1;
	}
	
    return 0;
}
