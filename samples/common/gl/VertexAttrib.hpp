#pragma once

#include <memory>
#include "gl/VertexArray.hpp"

namespace gl{

class VertexAttrib
{
	public:

		struct Config
		{
			GLint size;
			GLenum type;
			GLboolean normalized;
			GLuint relativeOffset;
		};

		VertexAttrib( std::shared_ptr<gl::VertexArray> VAO, GLuint index )
		{
			_VAO = VAO;
			_index = index;

			_VAO->addAttrib( index, this );

			glEnableVertexArrayAttrib( _VAO->getID(), _index );
		}

		~VertexAttrib()
		{
		}

		void setFormat(GLint size, GLenum type, GLboolean normalized, GLuint relativeOffset)
		{
			glVertexArrayAttribFormat( _VAO->getID(), _index, size, type, normalized, relativeOffset );
		}

		// 還沒試過
		void setFormat(struct Config config)
		{
			glVertexArrayAttribFormat( _VAO->getID(), _index, config.size, config.type, config.normalized, config.relativeOffset );
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
