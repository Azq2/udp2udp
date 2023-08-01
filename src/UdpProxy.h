#pragma once

#include <tuple>
#include <string>
#include <unordered_map>
#include <uv.h>
#include <cstddef>

#include "MemoryPool.h"

class UdpProxy {
	protected:
		static constexpr uint32_t MAX_UDP_SIZE = 64 * 1024;
		static constexpr uint32_t MAX_UDP_MESSAGES = 1;
		
		struct Client {
			struct sockaddr_in addr;
			uv_udp_t socket;
			uint64_t mtime;
			UdpProxy *parent;
		};
		
		struct TransferBuffer {
			char data[MAX_UDP_SIZE * MAX_UDP_MESSAGES];
			int refcount;
			
			inline void reset() {
				refcount = 0;
			}
			
			inline void ref() {
				refcount++;
			}
			
			inline void unref(MemoryPool<TransferBuffer> *mp) {
				refcount--;
				if (refcount == 0)
					mp->free(this);
			}
		};
		
		TransferBuffer *m_buffer = nullptr;
		
		struct sockaddr_in m_src = {};
		struct sockaddr_in m_dst = {};
		uv_loop_t *m_loop = nullptr;
		uv_udp_t m_server = {};
		bool m_debug = false;
		
		MemoryPool<TransferBuffer> *m_buffer_pool = nullptr;
		MemoryPool<uv_udp_send_t> *m_udp_pool = nullptr;
		
		std::unordered_map<uint64_t, Client> m_clients;
		
		void allocRecvBuffer(size_t suggested_size, uv_buf_t *buf);
		void clientRecvCb(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags);
		void serverRecvCb(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags);
		void udpSendCb(uv_udp_send_t *req, int status);
		
		inline static void encryptBuffer(uv_buf_t *buff) {
			size_t len = buff->len > 8 ? 8 : buff->len;
			for (size_t i = 0; i < len; i++)
				buff->base[i] ^= 0xAA;
		}
		
		Client *getClient(const struct sockaddr *addr);
	
	public:
		UdpProxy(const std::string &src, const std::string &dst);
		void run(uv_loop_t *loop);
};
