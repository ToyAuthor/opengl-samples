#include <string>
#include <fmt/core.h>    // 提供 fmt::print 來取代 std::printf
#include <glad/glad.h>   // 用來確認你電腦上的 OpenGL 版本，並準備相對應的 API 給你使用
#include <SDL.h>         // 負責建立視窗、處理來自作業系統的 event

#include "sdl/Window.hpp"         // 寫在 "opengl-samples/samples/common"，將 SDL2 的視窗工作、OpenGL 初始行為給包裝起來
#include "gl/ShaderProgram.hpp"   // 寫在 "opengl-samples/samples/common"，將 OpenGL 編譯著色語言的手續給包裝起來

namespace{

// 頂點著色器
const char* VertexShaderSource = R"(
	#version 460 core

	layout (location = 0) in vec3 aPos;
	layout (location = 1) in vec3 aColor;
	layout (location = 0) out vec3 ourColor;

	void main()
	{
		gl_Position = vec4( aPos, 1.0 );
		ourColor = aColor;
	}
)";

// 片元著色器
const char* FragmentShaderSource = R"(
	#version 460 core

	layout (location = 0) in  vec3 ourColor;
	layout (location = 0) out vec4 FragColor;
	layout (location = 0) uniform float timeOffset;

	void main()
	{
		FragColor = vec4( ourColor.r, ourColor.g * sin(timeOffset), ourColor.b, 1.0f );
	}
)";

// 三角形數據
constexpr float Vertices[] = {
	// 位置              // 顏色
	0.0f,   0.5f, 0.0f,  1.0f, 0.0f, 0.0f,    // 頂部 (紅)
	0.5f,  -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,    // 右下 (綠)
	-0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f     // 左下 (藍)
};

int main2()
{
	sdl::Window app;   // 使用 SDL2 來建立視窗

	if ( false == app.init( "sample 01", 800, 600 ) )
	{
		fmt::print( "視窗建立失敗\n" );
		return EXIT_FAILURE;
	}

	gl::ShaderProgram   myShader( VertexShaderSource, FragmentShaderSource );

	//--------------------------------------------------------------------------

	// 建立物件 (DSA：不需先 bind)
	GLuint   VAO = 0;
	GLuint   VBO = 0;
	glCreateVertexArrays( 1, &VAO );
	glCreateBuffers( 1, &VBO );

	// 配置緩衝區儲存空間，固定式存儲(Immutable Storage)，效能更好
	glNamedBufferStorage( VBO, sizeof( Vertices ), Vertices, 0 );

	// 1. 將 VBO 綁定到 VAO 的「第 0 號綁定點」，並設定步長為 6 個 float
	glVertexArrayVertexBuffer( VAO, 0, VBO, 0, 6 * sizeof( float ) );

	// 2. 啟用頂點屬性 0 (位置) 與 1 (顏色)
	glEnableVertexArrayAttrib( VAO, 0 );
	glEnableVertexArrayAttrib( VAO, 1 );

	// 3. 設定屬性格式
	glVertexArrayAttribFormat( VAO, 0, 3, GL_FLOAT, GL_FALSE, 0 );
	glVertexArrayAttribFormat( VAO, 1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ) );

	// 4. 告訴 VAO，屬性 0 和 1 都要去「第 0 號綁定點」拿資料
	glVertexArrayAttribBinding( VAO, 0, 0 );
	glVertexArrayAttribBinding( VAO, 1, 0 );

	//--------------------------------------------------------------------------

	bool       quit = false;
	SDL_Event  msg;

	/*
	 * 這迴圈的工作就兩件事：
	 * 1. 處理來自作業系統的 event
	 * 2. 描繪視窗上的畫面
	 */
	while ( false == quit )
	{
		// 處理來自作業系統的 event
		while ( SDL_PollEvent( &msg ) != 0 )
		{
			if ( msg.type == SDL_QUIT ) quit = true;
		}

		// 渲染：DSA 版本的清除畫面 (0 代表預設 framebuffer)
		constexpr GLfloat clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
		glClearNamedFramebufferfv( 0, GL_COLOR, 0, clearColor );

		// 更新參數：glProgramUniform 不需要先 glUseProgram
		const float timeValue = static_cast<float>( SDL_GetTicks() ) / 1000.0f;

		// 將 timeValue 傳送進 fragmentShaderSource 內的 timeOffset
		glProgramUniform1f(
			myShader.getID(),
			0,   // 數字 0 對應著 layout(location = 0)
			timeValue );

		myShader.use(); // 只有在真正要 Draw 的時候才 Bind Program

		// 渲染時只需要綁定 VAO 即可
		glBindVertexArray( VAO );
		glDrawArrays( GL_TRIANGLES, 0, 3 );

		/*
		 * 更新螢幕
		 * 如果你的螢幕裝置是 60 赫茲更新率的話
		 * 這裡就要花費 1/60 秒來更新視窗畫面
		 */
		app.refresh();
	}

	//--------------------------------------------------------------------------

	glDeleteVertexArrays( 1, &VAO );
	glDeleteBuffers( 1, &VBO );

	return EXIT_SUCCESS;
}

}

#undef main // 阻止 SDL2 使用 marco 來修改 "main" 這個名稱
int main()
{
	int result = EXIT_FAILURE;

	try
	{
		fmt::print( "執行程式\n" );
		result = main2();
	}
	catch ( const std::exception& e )
	{
		fmt::print( "異常訊息: {}\n", e.what() );
	}
	catch ( ... )
	{
		fmt::print( "捕獲到未知的異常\n" );
	}

	return result;
}
