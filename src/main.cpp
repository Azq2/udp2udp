#include "UdpProxy.h"

#include <cstring>
#include <iostream>
#include <uv.h>
#include <signal.h>

void signal_callback_handler(int signum) {
	std::cout << "Caught signal " << signum << "\n";
	exit(signum);
}

int main(int argc, char *argv[]) {
	signal(SIGINT, signal_callback_handler);
	
	auto *loop = uv_default_loop();
	
	UdpProxy proxy("127.0.0.1:1234", "8.8.8.8:53");
	proxy.run(loop);
	
	uv_run(loop, UV_RUN_DEFAULT);
	
    return 0;
}
