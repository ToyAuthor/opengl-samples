#pragma once

#include <vector>
#include <cstddef>
#include <glad/glad.h>

namespace gl{

// 這機制保護CPU與GPU之間使用記憶體時不會衝突
// 內容是 Persistent Mapping
// 使用 ring buffer 的方式
class StreamingBuffer
{
	public:

		// target 只能填
		// GL_UNIFORM_BUFFER 也就是 UBO
		// GL_SHADER_STORAGE_BUFFER 也就是 SSBO
		// GL_ATOMIC_COUNTER_BUFFER 也就是 Atomic Counter
		// GL_TRANSFORM_FEEDBACK_BUFFER 也就是 Transform Feedback
		// GL_ARRAY_BUFFER
		// GL_DRAW_INDIRECT_BUFFER
		// slotSize 為單一槽位所需大小，內部會依 target 的對齊需求向上取整
		StreamingBuffer(GLenum target, size_t slotSize, int ringCount = 3);
		~StreamingBuffer();

		// 禁用複製
		StreamingBuffer(const StreamingBuffer&) = delete;
		StreamingBuffer& operator=(const StreamingBuffer&) = delete;

		// 允許移動
		StreamingBuffer( StreamingBuffer&&) noexcept;
		StreamingBuffer& operator=( StreamingBuffer&&) noexcept;

		// 建構是否成功（驅動不支援 ARB_buffer_storage 時為 false）
		bool isValid() const;

		// 取得當前槽位的寫入指標，內部等待 GPU 釋放該槽位
		// 有可能阻塞，因為有使用 glClientWaitSync
		void* beginWrite();

		// 插入 fence 並切換至下一槽位
		void endWrite();

		// 嘗試取得當前槽位的寫入指標，如果 GPU 尚未釋放該槽位則返回 nullptr
		// 不會阻塞
		void* tryBeginWrite();

		// 綁定當前槽位至指定 binding point（UBO / SSBO 用）
		void bindRange(GLuint bindingIndex) const;

		GLuint    getBufferId() const;     // 就是用 glGenBuffers 產生的 buffer id
		GLenum    getTarget() const;
		GLintptr  getCurrentOffset() const;
		size_t    getSlotSize() const;     // 已對齊後的單槽大小
		size_t    getTotalSize() const;    // 總大小 = slotSize * ringCount ，這個應該沒人想知道吧？
		int       getRingCount() const;

	private:

		void release();
		void moveFrom( StreamingBuffer&& rhs) noexcept;

		GLenum               _target;
		GLuint               _bufferId;
		void*                _mappedPtr;   // 記住跟 OpenGL 申請的記憶體空間，CPU 可以直接寫入
		size_t               _slotSize;
		int                  _ringCount;
		int                  _index;
		std::vector<GLsync>  _fences;      // 是個指標清單，一個指標對應一個槽位，指標為 nullptr 表示該槽位可用
};

}
