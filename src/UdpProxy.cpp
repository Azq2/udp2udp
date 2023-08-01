#include "UdpProxy.h"
#include "Utils.h"

#include <iostream>

UdpProxy::UdpProxy(const std::string &src, const std::string &dst) {
	std::vector<std::string> tmp;
	tmp = strSplit(":", src);
	uv_ip4_addr(tmp[0].c_str(), strToInt(tmp[1]), &m_src);
	tmp = strSplit(":", dst);
	uv_ip4_addr(tmp[0].c_str(), strToInt(tmp[1]), &m_dst);
}

UdpProxy::Client *UdpProxy::getClient(const struct sockaddr *addr) {
	if (!addr)
		return nullptr;
	
	const struct sockaddr_in *addr_ip4 = reinterpret_cast<const struct sockaddr_in *>(addr);
	const uint64_t key = ((addr_ip4->sin_addr.s_addr << 16) | addr_ip4->sin_port);
	
	const auto it = m_clients.find(key);
	if (it == m_clients.cend()) {
		auto *client = &m_clients[key];
		
		std::cerr << "connected new client: " << ip2str(addr) << "\n";
		
		int ret = uv_udp_init_ex(m_loop, &client->socket, MAX_UDP_MESSAGES > 1 ? UV_UDP_RECVMMSG : 0);
		if (ret < 0) {
			std::cerr << "uv_udp_init failed: " << uv_strerror(ret) << "\n";
			return nullptr;
		}
		
		static const auto alloc_cb = +[](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
			auto *self = reinterpret_cast<Client *>(handle->data)->parent;
			self->allocRecvBuffer(suggested_size, buf);
		};
		
		static const auto recv_cb = +[](uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
			auto *self = reinterpret_cast<Client *>(handle->data)->parent;
			self->clientRecvCb(handle, nread, buf, addr, flags);
		};
		
		ret = uv_udp_recv_start(&client->socket, alloc_cb, recv_cb);
		if (ret < 0) {
			std::cerr << "uv_udp_recv_start failed: " << uv_strerror(ret) << "\n";
			return nullptr;
		}
		
		client->socket.data = client;
		client->addr = *addr_ip4;
		client->parent = this;
		
		return client;
	}
	
	return &it->second;
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
	// std::cerr << "send " << nread << " bytes from " << ip2str(addr) << " to " << ip2str(send_addr) << "\n";
	
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
	
	auto *send_addr = reinterpret_cast<const struct sockaddr *>(&m_dst);
	// std::cerr << "send " << nread << " bytes from " << ip2str(addr) << " to " << ip2str(send_addr) << "\n";
	
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
	
	if (status < 0) {
		std::cerr << "uv_udp_send failed: " << uv_strerror(status) << "\n";
	}
}

void UdpProxy::run(uv_loop_t *loop) {
	int ret;
	
	m_buffer_pool = new MemoryPool<TransferBuffer>(100);
	m_udp_pool = new MemoryPool<uv_udp_send_t>(100);
	
	m_loop = loop;
	
	ret = uv_udp_init_ex(m_loop, &m_server, MAX_UDP_MESSAGES > 1 ? UV_UDP_RECVMMSG : 0);
	if (ret < 0) {
		std::cerr << "uv_udp_init failed: " << uv_strerror(ret) << "\n";
		return;
	}
	
	m_server.data = this;
	
	ret = uv_udp_bind(&m_server, (const struct sockaddr *) &m_src, 0);
	if (ret < 0) {
		std::cerr << "uv_udp_bind failed: " << uv_strerror(ret) << "\n";
		return;
	}
	
	static const auto alloc_cb = +[](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
		auto *self = reinterpret_cast<UdpProxy *>(handle->data);
		self->allocRecvBuffer(suggested_size, buf);
	};
	
	static const auto recv_cb = +[](uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
		auto *self = reinterpret_cast<UdpProxy *>(handle->data);
		self->serverRecvCb(handle, nread, buf, addr, flags);
	};
	
	ret = uv_udp_recv_start(&m_server, alloc_cb, recv_cb);
	if (ret < 0) {
		std::cerr << "uv_udp_recv_start failed: " << uv_strerror(ret) << "\n";
		return;
	}
}
