#pragma once

namespace gl{

/*
 * OpenGL 使用全域狀態來管理顯卡
 * 我這邊刻意將這種全域操作又包裝成一個物件
 * 為了強調資源的生命週期，避免忘記釋放資源
 * 就用這個 Core 物件來代表整個顯卡
 */
class Core
{
	public:

		Core() = default;
		~Core() = default;

	//--------------------------------------------------------------------------

		// 這函式交給 gl::VertexArray 內部來呼叫
		void _bindVAO( GLuint id_VAO )
		{
			// 檢查一下，避免重複綁定同一個 VAO
			if ( _id_VAO != id_VAO )
			{
				_id_VAO = id_VAO;
				glBindVertexArray( _id_VAO );
			}
		}

	private:
	
		GLuint _id_VAO = 0;  // 紀錄目前綁定的 VAO ID，避免重複綁定

	//--------------------------------------------------------------------------

	public:

		void clear( GLfloat r, GLfloat g, GLfloat b, GLfloat a )
		{
			_clearColor[0] = r;
			_clearColor[1] = g;
			_clearColor[2] = b;
			_clearColor[3] = a;

			glClearNamedFramebufferfv( 0, GL_COLOR, 0, _clearColor );
		}

		void clear()
		{
			glClearNamedFramebufferfv( 0, GL_COLOR, 0, _clearColor );
		}

	private:

		GLfloat _clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
};

}
