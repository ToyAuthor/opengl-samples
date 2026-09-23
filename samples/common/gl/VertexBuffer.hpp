#pragma once

namespace gl{

// 包裝 VBO(Vertex Buffer Object)
class VertexBuffer
{
	public:

		VertexBuffer( GLsizeiptr size, const void *data )
		{
			glCreateBuffers( 1, &_id );
			glNamedBufferStorage( _id, size, data, 0 );
		}

		~VertexBuffer()
		{
			glDeleteBuffers( 1, &_id );
		}

		GLuint getID()
		{
			return _id;
		}

	private:

		GLuint _id = 0;
};

}
