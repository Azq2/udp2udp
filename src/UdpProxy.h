#pragma once

#include <tuple>
#include <string>
#include <unordered_map>
#include <uv.h>
#include <cstddef>

#include "MemoryPool.h"

class UdpProxy {
	protected:
		static constexpr uint32_t TIMEOUT = 60000;
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
		std::vector<struct sockaddr_in> m_dst = {};
		size_t m_dst_cursor = 0;
		uv_loop_t *m_loop = nullptr;
		uv_udp_t m_server = {};
		bool m_debug = false;
		uv_timer_t m_cleaner_timer = {};
		uint8_t m_xor_key = 0;
		size_t m_xor_size = 0;
		
		MemoryPool<TransferBuffer> *m_buffer_pool = nullptr;
		MemoryPool<uv_udp_send_t> *m_udp_pool = nullptr;
		
		std::unordered_map<uint64_t, Client *> m_clients;
		
		void allocRecvBuffer(size_t suggested_size, uv_buf_t *buf);
		void clientRecvCb(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags);
		void serverRecvCb(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags);
		void udpSendCb(uv_udp_send_t *req, int status);
		void cleanupOldConnections();
		
		inline void encryptBuffer(uv_buf_t *buff) {
			if (m_xor_size) {
				size_t len = buff->len > m_xor_size ? m_xor_size : buff->len;
				for (size_t i = 0; i < len; i++)
					buff->base[i] ^= m_xor_key;
			}
		}
		
		Client *getClient(const struct sockaddr *addr);
	
	public:
		UdpProxy(const std::string &src, const std::string &dst_list);
		
		inline void setXorEncryption(uint8_t key, size_t max_len) {
			m_xor_key = key;
			m_xor_size = max_len;
		}
		
		void run(uv_loop_t *loop);
};
