#pragma once

namespace gl{

class ShaderProgram
{
	public:

		ShaderProgram( const char* vs, const char* fs )
		{
			GLuint vsID = CompileShader( GL_VERTEX_SHADER, vs );
			GLuint fsID = CompileShader( GL_FRAGMENT_SHADER, fs );

			_id = glCreateProgram();

			glAttachShader( _id, vsID );
			glAttachShader( _id, fsID );

			glLinkProgram( _id );

			// 連結完成後即可卸載並刪除 shader object，避免資源洩漏
			glDetachShader( _id, vsID );
			glDetachShader( _id, fsID );
			glDeleteShader( vsID );
			glDeleteShader( fsID );

			// 檢查 Program 連結狀態
			GLint linkSuccess;

			glGetProgramiv( _id, GL_LINK_STATUS, &linkSuccess );

			if ( GL_FALSE==linkSuccess )
			{
				char infoLog[512];
				glGetProgramInfoLog( _id, 512, nullptr, infoLog );
				fmt::print( "Shader Program Link Error: {}\n", infoLog );
			}
		}

		~ShaderProgram()
		{
			release();
		}

		void use()
		{
			glUseProgram( _id );
		}

		void unuse()
		{
			glUseProgram(0);
		}

		void release()
		{
			if ( _id != 0 )
			{
				glDeleteProgram( _id );
				_id = 0;
			}
		}

		GLuint getID()
		{
			return _id;
		}

	private:

		GLuint _id = 0;

		static GLuint CompileShader( GLenum type, const char* src )
		{
			GLuint shader = glCreateShader( type );

			glShaderSource( shader, 1, &src, nullptr );
			glCompileShader( shader );

			GLint success;

			glGetShaderiv( shader, GL_COMPILE_STATUS, &success );

			if ( !success )
			{
				char infoLog[512];
				glGetShaderInfoLog( shader, 512, nullptr, infoLog );
				fmt::print( "SHADER COMPILE ERROR ({}):\n{}\n", ( type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT" ), infoLog );
			}

			return shader;
		};
};

}
