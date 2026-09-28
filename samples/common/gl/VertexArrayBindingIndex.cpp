#include <memory>
#include <fmt/core.h>
#include <glad/glad.h>
#include "gl/VertexAttrib.hpp"
#include "gl/VertexArrayBindingIndex.hpp"

void gl::VertexArrayBindingIndex::bindAttrib( GLuint attribID )
{
	glVertexArrayAttribBinding( _VAO, attribID, _bindingIndex );
}

void gl::VertexArrayBindingIndex::bind( std::shared_ptr<gl::VertexAttrib> attrib )
{
	glVertexArrayAttribBinding( _VAO, attrib->getIndex(), _bindingIndex );
}

void gl::VertexArrayBindingIndex::bindVBO( GLuint buffer, GLintptr offset, GLsizei stride )
{
	glVertexArrayVertexBuffer( _VAO, _bindingIndex, buffer, offset, stride );
}

void gl::VertexArrayBindingIndex::setDivisor( GLuint divisor )
{
	// divisor = 0，每個頂點更新一次，這是預設值(非 instanced 屬性)
	// divisor = 1，每個 instance 更新一次(一張圖片就是一個 instance)
	// divisor = 2，每兩個 instance 更新一次
	glVertexArrayBindingDivisor( _VAO, _bindingIndex, divisor );
}
