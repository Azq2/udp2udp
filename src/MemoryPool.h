#pragma once

#include <vector>

template <typename T>
class MemoryPool {
	protected:
		struct ItemWrapper {
			T value;
			ItemWrapper *next;
		};
		
		size_t m_block_size = 0;
		std::vector<ItemWrapper *> m_storage;
		
		ItemWrapper *m_first = nullptr;
		ItemWrapper *m_last = nullptr;
		
		void allocateBlock() {
			auto *chunk = new ItemWrapper[m_block_size];
			m_storage.push_back(chunk);
			
			for (size_t i = 0; i < m_block_size; i++)
				pushItem(&chunk[i]);
		}
		
		inline bool hasFreeItems() {
			return m_first != nullptr;
		}
		
		ItemWrapper *shiftItem() {
			ItemWrapper *result = m_first;
			if (m_first) {
				if (m_first->next) {
					m_first = m_first->next;
				} else {
					m_first = nullptr;
					m_last = nullptr;
				}
			}
			return result;
		}
		
		void pushItem(ItemWrapper *item) {
			if (m_first) {
				m_last->next = item;
				item->next = nullptr;
				m_last = item;
			} else {
				m_first = item;
				m_last = item;
				item->next = nullptr;
			}
		}
	public:
		MemoryPool(size_t block_size) {
			m_block_size = block_size;
			allocateBlock();
		}
		
		T *alloc() {
			if (!hasFreeItems())
				allocateBlock();
			return &shiftItem()->value;
		}
		
		void free(T *value) {
			auto *item = reinterpret_cast<ItemWrapper *>(value);
			pushItem(item);
		}
};
