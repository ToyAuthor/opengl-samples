#pragma once

namespace sdl{

// 採用了 GLAD
// https://glad.dav1d.de/
// https://github.com/Dav1dde/glad
// https://github.com/dav1dde/glad-web
class Window
{
	public:

		Window()
		{
			;
		}

		~Window()
		{
			try
			{
				destroy();
			}
			catch ( const std::exception& e )
			{
				fmt::print( "sdl::Window exception: {}\n", e.what() );
			}
			catch ( ... )
			{
				fmt::print( "sdl::Window Unknown exception\n" );
			}
		}

		bool init(std::string title,int width,int height)
		{
			if ( SDL_Init( SDL_INIT_VIDEO ) < 0 )
			{
				fmt::print( "SDL_Init 失敗: {}\n", SDL_GetError() );
				return false;
			}

			_sdlInited = true;

			// 設定 OpenGL 版本與 Profile (4.6 Core)
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 4 );
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 6 );
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
			SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 1 );   // 開啟多重採樣來抗鋸齒
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 4 );   // 設定採樣次數(通常為 4, 8 或 16，數字愈大效果愈好但愈耗效能)
			SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );         // 有進行 stencil test 才會需要開啟
			SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 );   // 啟用「硬體加速」，通常驅動預設就會啟用

		#ifdef _DEBUG
			// 要求開啟 debug context，glDebugMessageCallback 才能完整運作
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG );
			// 會移除某些舊功能，可有可無的選項
		//	SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG | SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG );
		#endif

			_window = SDL_CreateWindow( title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL );

			if ( !_window )
			{
				fmt::print( "SDL_CreateWindow 失敗: {}\n", SDL_GetError() );
				destroy();
				return false;
			}

			_context = SDL_GL_CreateContext( _window );

			// 如果 OpenGL context 建立失敗，有可能是因為 OpenGL 不是 4.6 版
			if ( !_context )
			{
				fmt::print( "SDL_GL_CreateContext 失敗: {}\n", SDL_GetError() );
				destroy();
				return false;
			}

			// 用 GLAD 初始化
			if ( !gladLoadGLLoader( (GLADloadproc)SDL_GL_GetProcAddress ) )
			{
				fmt::print( "gladLoadGLLoader 初始化失敗\n" );
				destroy();
				return false;
			}

			// 確認 OpenGL 版本是不是 4.6 版
			{
				GLint major = 0;
				GLint minor = 0;

				glGetIntegerv( GL_MAJOR_VERSION, &major );
				glGetIntegerv( GL_MINOR_VERSION, &minor );

				if ( major != 4 || minor != 6 )
				{
					fmt::print( "需要 OpenGL 4.6，目前版本為 {}.{} ({})\n", major, minor, reinterpret_cast<const char*>( glGetString( GL_VERSION ) ) );

					destroy();
					return false;
				}
			}

			// 開 VSync，避免渲染迴圈空轉燒 CPU
			if ( SDL_GL_SetSwapInterval( 1 ) < 0 )
			{
				fmt::print( "無法啟用 VSync: {}\n", SDL_GetError() );
			}

			if ( !GLAD_GL_ARB_direct_state_access )
			{
				fmt::print( "此驅動不支援 ARB_direct_state_access\n" );
				destroy();
				return false;
			}

			if ( !GLAD_GL_ARB_bindless_texture )
			{
				fmt::print( "此驅動不支援 ARB_bindless_texture\n" );
				destroy();
				return false;
			}

			// glDebugMessageCallback 自 GL 4.3 起為核心功能
			if ( GLAD_GL_KHR_debug || GLAD_GL_ARB_debug_output )
			{
				glEnable( GL_DEBUG_OUTPUT );
				glEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS );

				glDebugMessageCallback( [](
					GLenum source,
					GLenum type,
					GLuint id,
					GLenum severity,
					GLsizei length,
					const GLchar* message,
					const void* userParam )
				{
					(void)source;
					(void)type;
					(void)id;
					(void)length;
					(void)userParam;

					if ( severity == GL_DEBUG_SEVERITY_NOTIFICATION ) return;

					fmt::print( "GL DEBUG: {}\n", message );
				}, nullptr );

				// 確保所有訊息都會送達 callback
				glDebugMessageControl( GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE );
			}

			return true;
		}

		void refresh()
		{
			SDL_GL_SwapWindow( _window );
		}

		void destroy()
		{
			// 清為 nullptr，讓重複呼叫 destroy() 是安全的
			if ( _context )
			{
				SDL_GL_DeleteContext( _context );
				_context = nullptr;
			}

			if ( _window )
			{
				SDL_DestroyWindow( _window );
				_window = nullptr;
			}

			if ( _sdlInited )
			{
				SDL_Quit();
				_sdlInited = false;
			}
		}

	private:

		SDL_Window*     _window      = nullptr;
		SDL_GLContext   _context     = nullptr;
		bool            _sdlInited   = false;
};

}
