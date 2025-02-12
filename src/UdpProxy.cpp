#include "UdpProxy.h"
#include "Utils.h"

#include <iostream>

UdpProxy::UdpProxy(const std::string &src, const std::string &dst_list) {
	std::vector<std::string> tmp;
	tmp = strSplit(":", src);
	if (uv_ip4_addr(tmp[0].c_str(), strToInt(tmp[1]), &m_src) != 0)
		throw std::runtime_error(strprintf("Invalid address: %s", src.c_str()));
	
	for (auto &dst: strSplit(",", dst_list)) {
		tmp = strSplit(":", dst);
		m_dst.resize(m_dst.size() + 1);
		if (uv_ip4_addr(tmp[0].c_str(), strToInt(tmp[1]), &m_dst.back()) != 0)
			throw std::runtime_error(strprintf("Invalid address: %s", dst.c_str()));
	}
}

UdpProxy::Client *UdpProxy::getClient(const struct sockaddr *addr) {
	if (!addr)
		return nullptr;
	
	const struct sockaddr_in *addr_ip4 = reinterpret_cast<const struct sockaddr_in *>(addr);
	const uint64_t key = ((addr_ip4->sin_addr.s_addr << 24) | (addr_ip4->sin_port << 8));
	
	const auto it = m_clients.find(key);
	if (it == m_clients.cend()) {
		auto *client = new Client();
		
		std::cerr << "connected new client: " << ip2str(addr) << "\n";
		
		int ret = uv_udp_init_ex(m_loop, &client->socket, MAX_UDP_MESSAGES > 1 ? UV_UDP_RECVMMSG : 0);
		if (ret < 0)
			throw std::runtime_error(strprintf("uv_udp_init_ex failed: %s", uv_strerror(ret)));
		
		static const auto alloc_cb = +[](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
			auto *self = reinterpret_cast<Client *>(handle->data)->parent;
			self->allocRecvBuffer(suggested_size, buf);
		};
		
		static const auto recv_cb = +[](uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
			auto *self = reinterpret_cast<Client *>(handle->data)->parent;
			self->clientRecvCb(handle, nread, buf, addr, flags);
		};
		
		ret = uv_udp_recv_start(&client->socket, alloc_cb, recv_cb);
		if (ret < 0)
			throw std::runtime_error(strprintf("uv_udp_recv_start failed: %s", uv_strerror(ret)));
		
		client->socket.data = client;
		client->addr = *addr_ip4;
		client->parent = this;
		
		m_clients[key] = client;
		
		return client;
	}
	
	return it->second;
}

void UdpProxy::allocRecvBuffer(size_t suggested_size, uv_buf_t *buf) {
	m_buffer = m_buffer_pool->alloc();
	m_buffer->reset();
	m_buffer->ref();
	
	buf->base = m_buffer->data;
	buf->len = sizeof(m_buffer->data);
}

void UdpProxy::clientRecvCb(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
	auto *client = reinterpret_cast<Client *>(handle->data);
	
	if ((flags & UV_UDP_MMSG_FREE) || !addr) {
		m_buffer->unref(m_buffer_pool);
		return;
	}
	
	auto *send_addr = reinterpret_cast<const struct sockaddr *>(&client->addr);
	//std::cerr << "send " << nread << " bytes from " << ip2str(addr) << " to " << ip2str(send_addr) << "\n";
	
	uv_buf_t send_buf = uv_buf_init(buf->base, nread);
	encryptBuffer(&send_buf);
	
	if ((flags & UV_UDP_MMSG_CHUNK))
		m_buffer->ref();
	
	static const auto send_cb = +[](uv_udp_send_t *req, int status) {
		auto *self = reinterpret_cast<UdpProxy *>(req->handle->data);
		self->udpSendCb(req, status);
	};
	
	auto udp = m_udp_pool->alloc();
	udp->data = m_buffer;
	uv_udp_send(udp, &m_server, &send_buf, 1, send_addr, send_cb);
}

void UdpProxy::serverRecvCb(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
	if ((flags & UV_UDP_MMSG_FREE) || !addr) {
		m_buffer->unref(m_buffer_pool);
		return;
	}
	
	Client *client = getClient(addr);
	if (!client)
		return;
	
	client->mtime = uv_now(m_loop);
	
	auto *send_addr = reinterpret_cast<const struct sockaddr *>(&m_dst[m_dst_cursor]);
	m_dst_cursor = (m_dst_cursor + 1) % m_dst.size();
	//std::cerr << "send " << nread << " bytes from " << ip2str(addr) << " to " << ip2str(send_addr) << "\n";
	
	uv_buf_t send_buf = uv_buf_init(buf->base, nread);
	encryptBuffer(&send_buf);
	
	if ((flags & UV_UDP_MMSG_CHUNK))
		m_buffer->ref();
	
	static const auto send_cb = +[](uv_udp_send_t *req, int status) {
		auto *self = reinterpret_cast<Client *>(req->handle->data)->parent;
		self->udpSendCb(req, status);
	};
	
	auto udp = m_udp_pool->alloc();
	udp->data = m_buffer;
	uv_udp_send(udp, &client->socket, &send_buf, 1, send_addr, send_cb);
}

void UdpProxy::udpSendCb(uv_udp_send_t *req, int status) {
	TransferBuffer *buffer = reinterpret_cast<TransferBuffer *>(req->data);
	buffer->unref(m_buffer_pool);
	m_udp_pool->free(req);
	
	/*
	if (status < 0) {
		std::cerr << "uv_udp_send failed: " << uv_strerror(status) << "\n";
	}
	*/
}

void UdpProxy::cleanupOldConnections() {
	auto now = uv_now(m_loop);
	for (auto it = m_clients.begin(); it != m_clients.end(); ) {
		auto client = it->second;
		if (now - client->mtime >= TIMEOUT) {
			it = m_clients.erase(it);
			
			static const auto close_cb = +[](uv_handle_t *handle) {
				auto *client = reinterpret_cast<Client *>(handle->data);
				delete client;
			};
			
			uv_udp_recv_stop(&client->socket);
			uv_close(reinterpret_cast<uv_handle_t *>(&client->socket), close_cb);
		} else {
			it++;
		}
	}
}

void UdpProxy::run(uv_loop_t *loop) {
	int ret;
	
	m_buffer_pool = new MemoryPool<TransferBuffer>(100);
	m_udp_pool = new MemoryPool<uv_udp_send_t>(100);
	
	m_loop = loop;
	
	ret = uv_udp_init_ex(m_loop, &m_server, MAX_UDP_MESSAGES > 1 ? UV_UDP_RECVMMSG : 0);
	if (ret < 0)
		throw std::runtime_error(strprintf("uv_udp_init_ex failed: %s", uv_strerror(ret)));
	
	m_server.data = this;
	
	ret = uv_udp_bind(&m_server, (const struct sockaddr *) &m_src, 0);
	if (ret < 0)
		throw std::runtime_error(strprintf("uv_udp_bind(%s) failed: %s", ip2str(&m_src).c_str(), uv_strerror(ret)));
	
	static const auto alloc_cb = +[](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
		auto *self = reinterpret_cast<UdpProxy *>(handle->data);
		self->allocRecvBuffer(suggested_size, buf);
	};
	
	static const auto recv_cb = +[](uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
		auto *self = reinterpret_cast<UdpProxy *>(handle->data);
		self->serverRecvCb(handle, nread, buf, addr, flags);
	};
	
	ret = uv_udp_recv_start(&m_server, alloc_cb, recv_cb);
	if (ret < 0)
		throw std::runtime_error(strprintf("uv_udp_recv_start failed: %s", uv_strerror(ret)));
	
	static const auto cleaner_cb = +[](uv_timer_t *handle) {
		auto *self = reinterpret_cast<UdpProxy *>(handle->data);
		self->cleanupOldConnections();
	};
	
	m_cleaner_timer.data = this;
	uv_timer_init(m_loop, &m_cleaner_timer);
	uv_timer_start(&m_cleaner_timer, cleaner_cb, TIMEOUT, TIMEOUT);
}
