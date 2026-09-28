#pragma once

#include <memory>
#include "gl/VertexArray.hpp"

namespace gl{

// 記錄一個 VAO 的頂點屬性(Vertex Attribute)
class VertexAttrib
{
	public:

		// 因為是本類別需要 VAO，VAO 並不關心本類別建立的物件是否存在
		// 所以使用 shared_ptr 來留住 VAO
		VertexAttrib( std::shared_ptr<gl::VertexArray> VAO, GLuint index )
		{
			_VAO = VAO;
			_index = index;

			_VAO->_addAttrib( index, this );

			glEnableVertexArrayAttrib( _VAO->_getID(), _index );
		}

		~VertexAttrib()
		{
			glDisableVertexArrayAttrib( _VAO->_getID(), _index );
		}

		void setFormat(GLint size, GLenum type, GLboolean normalized, GLuint relativeOffset)
		{
			glVertexArrayAttribFormat( _VAO->_getID(), _index, size, type, normalized, relativeOffset );
		}

		void setFormat(GLint size, GLenum type, GLuint relativeOffset)
		{
			glVertexArrayAttribIFormat( _VAO->_getID(), _index, size, type, relativeOffset );
		}

		GLuint getIndex()
		{
			return _index;
		}

	private:

		std::shared_ptr<gl::VertexArray>  _VAO = nullptr;
		GLuint _index = 0;
};

}
