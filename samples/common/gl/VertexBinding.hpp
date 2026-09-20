#pragma once

#include <memory>
#include "gl/VertexArray.hpp"
#include "gl/VertexAttrib.hpp"

namespace gl{

class VertexBinding
{
	public:

		VertexBinding( std::shared_ptr<gl::VertexArray> VAO )
		{
			_VAO = VAO;
			_bindingIndex = _VAO->addBinding(this);  // 讓 gl::VertexArray 來決定綁定點是幾號
		}

		~VertexBinding()
		{

		}

		void attachAttrib( std::shared_ptr<gl::VertexAttrib> ptr )
		{
			glVertexArrayAttribBinding( _VAO->getID(), ptr->getIndex(), _bindingIndex );
		}

		void bindVBO( GLuint buffer, GLintptr offset, GLsizei stride )
		{
			glVertexArrayVertexBuffer( _VAO->getID(), _bindingIndex, buffer, offset, stride );
		}

	private:

		std::shared_ptr<VertexArray> _VAO = nullptr;
		GLuint _bindingIndex = 0;
};

}
