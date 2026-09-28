#pragma once

namespace gl{

class VertexAttrib;

/*
 * 這類別算是 gl::VertexArray 的附屬品
 * 名字這麼長無所謂
 * 反正使用的時候不會看到
 */
class VertexArrayBindingIndex
{
	public:

		VertexArrayBindingIndex( GLuint bindingIndex ):
			_bindingIndex( bindingIndex )
		{
		}

		~VertexArrayBindingIndex() = default;

		void bindAttrib( GLuint attribID );
		void bind( std::shared_ptr<gl::VertexAttrib> attrib );
		void bindVBO( GLuint buffer, GLintptr offset, GLsizei stride );
		void setDivisor( GLuint divisor );

	private:

		GLuint _bindingIndex;

	public:

		GLuint _VAO = 0;   // 只能由 gl::VertexArray 賦值，並且賦值之後就不更改了
};

}
