#include <utility>   // 為了使用 std::move 而引入的
#include <fmt/core.h>
#include "gl/ImageData.hpp"
#include "gl/CreateImage.hpp"
#include "gl/StreamingBuffer.hpp"

namespace {

size_t QueryAlignment( GLenum target )
{
	GLint align = 1;

	switch ( target )
	{
		case GL_UNIFORM_BUFFER:
			glGetIntegerv( GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align );
			break;

		case GL_SHADER_STORAGE_BUFFER:
			glGetIntegerv( GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &align );
			break;

		case GL_ATOMIC_COUNTER_BUFFER:
			// 規格並未提供可查詢的 offset alignment 常數，
			// spec 僅保證 offset 需為 sizeof(GLuint) 的倍數
			align = static_cast< GLint >( sizeof( GLuint ) );
			break;

		case GL_TRANSFORM_FEEDBACK_BUFFER:
			align = 16;
			break;

		case GL_ARRAY_BUFFER:
			// 頂點緩衝區（VBO）沒有硬性規定的 offset alignment，
			// 這裡以 4 bytes(float 的大小) 對齊即可滿足絕大多數頂點格式
			align = static_cast< GLint >( sizeof( float ) );
			break;

		case GL_DRAW_INDIRECT_BUFFER:
			// Indirect Draw Command Buffer(給 glMultiDrawElementsIndirect 用)
			// 規格並未提供可查詢的 offset alignment 常數，
			// 只要求 offset 必須是 4 的倍數(GLuint 大小)
			align = static_cast< GLint >( sizeof( GLuint ) );
			break;

		default:
			throw std::runtime_error( "Unsupported buffer target for alignment query");
	}

	return ( align > 0 ) ? static_cast< size_t >( align ) : 1;
}

size_t AlignUp( size_t value, size_t align )
{
	return ( value + align - 1 ) / align * align;
}

}

gl::StreamingBuffer::StreamingBuffer( GLenum target, size_t slotSize, int ringCount )
	: _target( target )
	, _bufferId( 0 )
	, _mappedPtr( nullptr )
	, _slotSize( 0 )
	, _ringCount( ringCount > 0 ? ringCount : 1 )
	, _index( 0 )
{
	_slotSize = AlignUp( slotSize, QueryAlignment( target ) );

	// storage flags：只能給 glNamedBufferStorage 合法使用的位元
	const GLbitfield storageFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

	// map flags：額外加上 UNSYNCHRONIZED，因為同步已由 _fences 手動管理，
	// 避免驅動在 map 階段做多餘的隱性等待
	const GLbitfield mapFlags = storageFlags | GL_MAP_UNSYNCHRONIZED_BIT;

	const size_t totalSize = _slotSize * static_cast< size_t >( _ringCount );

	glCreateBuffers( 1, &_bufferId );
	glNamedBufferStorage( _bufferId, static_cast< GLsizeiptr >( totalSize ), nullptr, storageFlags );

	_mappedPtr = glMapNamedBufferRange( _bufferId, 0, static_cast< GLsizeiptr >( totalSize ), mapFlags );

	if ( _mappedPtr == nullptr )
	{
		glDeleteBuffers( 1, &_bufferId );
		_bufferId = 0;
		_slotSize = 0;
		return;
	}

	_fences.resize( static_cast< size_t >( _ringCount ), nullptr );
}

gl::StreamingBuffer::~StreamingBuffer()
{
	release();
}

gl::StreamingBuffer::StreamingBuffer( StreamingBuffer&& rhs ) noexcept
	: _target( GL_ARRAY_BUFFER )
	, _bufferId( 0 )
	, _mappedPtr( nullptr )
	, _slotSize( 0 )
	, _ringCount( 1 )
	, _index( 0 )
{
	moveFrom( std::move( rhs ) );
}

gl::StreamingBuffer& gl::StreamingBuffer::operator=( StreamingBuffer&& rhs ) noexcept
{
	if ( this != &rhs )
	{
		release();
		moveFrom( std::move( rhs ) );
	}

	return *this;
}

void gl::StreamingBuffer::moveFrom( StreamingBuffer&& rhs ) noexcept
{
	_target    = rhs._target;
	_bufferId  = rhs._bufferId;
	_mappedPtr = rhs._mappedPtr;
	_slotSize  = rhs._slotSize;
	_ringCount = rhs._ringCount;
	_index     = rhs._index;
	_fences    = std::move( rhs._fences );

	rhs._bufferId  = 0;
	rhs._mappedPtr = nullptr;
	rhs._slotSize  = 0;
	rhs._index     = 0;
	rhs._fences.clear();
}

void gl::StreamingBuffer::release()
{
	for ( size_t i = 0 ; i < _fences.size() ; ++i )
	{
		if ( _fences[ i ] != nullptr )
		{
			glDeleteSync( _fences[ i ] );
			_fences[ i ] = nullptr;
		}
	}

	_fences.clear();

	if ( _bufferId != 0 )
	{
		if ( _mappedPtr != nullptr )
		{
			glUnmapNamedBuffer( _bufferId );
			_mappedPtr = nullptr;
		}

		glDeleteBuffers( 1, &_bufferId );
		_bufferId = 0;
	}
}

bool gl::StreamingBuffer::isValid() const
{
	return _mappedPtr != nullptr;
}

void* gl::StreamingBuffer::beginWrite()
{
	if ( !isValid() )
	{
		return nullptr;
	}

	GLsync& fence = _fences[ static_cast< size_t >( _index ) ];

	if ( fence != nullptr )
	{
		// 先以 0 timeout 試探，查詢是否 GPU 已完成該槽位的使用
		GLenum result = glClientWaitSync( fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0 );

		if ( result == GL_TIMEOUT_EXPIRED )
		{
			// GPU 尚未完成，進入無限等待直到完成
			glClientWaitSync( fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED );
		}

		glDeleteSync( fence );
		fence = nullptr;
	}

	return static_cast< char* >( _mappedPtr ) + getCurrentOffset();
}

void gl::StreamingBuffer::endWrite()
{
	if ( !isValid() )
	{
		return;
	}

	// COHERENT 對應，不需 glFlushMappedBufferRange
	_fences[ static_cast< size_t >( _index ) ] = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );

	_index = ( _index + 1 ) % _ringCount;
}

void gl::StreamingBuffer::bindRange( GLuint bindingIndex ) const
{
	if ( !isValid() )
	{
		return;
	}

	glBindBufferRange( _target, bindingIndex, _bufferId, getCurrentOffset(), static_cast<GLsizeiptr>( _slotSize ) );
}

GLuint gl::StreamingBuffer::getBufferId() const
{
	return _bufferId;
}

GLenum gl::StreamingBuffer::getTarget() const
{
	return _target;
}

GLintptr gl::StreamingBuffer::getCurrentOffset() const
{
	return static_cast< GLintptr >( _slotSize * static_cast< size_t >( _index ) );
}

size_t gl::StreamingBuffer::getSlotSize() const
{
	return _slotSize;
}

size_t gl::StreamingBuffer::getTotalSize() const
{
	return _slotSize * static_cast< size_t >( _ringCount );
}

int gl::StreamingBuffer::getRingCount() const
{
	return _ringCount;
}

void* gl::StreamingBuffer::tryBeginWrite()
{
	if ( !isValid() )
	{
		return nullptr;
	}

	GLsync& fence = _fences[ static_cast< size_t >( _index ) ];

	if ( fence != nullptr )
	{
		const GLenum result = glClientWaitSync( fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0 );

		if ( result == GL_TIMEOUT_EXPIRED )
		{
			return nullptr;
		}

		glDeleteSync( fence );
		fence = nullptr;
	}

	return static_cast< char* >( _mappedPtr ) + getCurrentOffset();
}
