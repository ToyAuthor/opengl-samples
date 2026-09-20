#include <string>
#include <fmt/core.h>    // 提供 fmt::print 來取代 std::printf
#include <glad/glad.h>   // 用來跟顯卡驅動程式溝通，並取出顯卡所支援的 OpenGL API 給你使用
#include <SDL.h>         // 負責建立視窗、處理來自作業系統的 event

//----------------------寫在 "opengl-samples/samples/common"----------------------
#include "sdl/Utils.hpp"          // 提供一些基於 SDL2 的通用工具
#include "sdl/Window.hpp"         // 將 SDL2 的視窗工作、OpenGL 初始行為給包裝起來
#include "gl/Utils.hpp"           // 提供一些基於 OpenGL 的通用工具
#include "gl/ShaderProgram.hpp"   // 將 OpenGL 編譯著色語言的手續給包裝起來

namespace{

// vertex 著色器，用來計算出座標點的位置
const char* VertexShaderSource = R"(
	// 460 是版號，指的是 OpenGL 4.6
	#version 460 core

	// VBO 透過 glNamedBufferStorage 將 Vertices 的數據資料傳進來
	// location 數字就是 glEnableVertexArrayAttrib 設定的屬性編號
	layout (location = 0) in vec3 aPos;
	layout (location = 1) in vec3 aColor;
	layout (location = 0) out vec3 ourColor;  // out ourColor 所用的 location 數字必須跟 FragmentShaderSource 的 in ourColor 相符

	void main()
	{
		// gl_Position 會填上 vertex shader 最終計算出的座標
		gl_Position = vec4( aPos, 1.0 );
		ourColor = aColor;
	}
)";

// fragment著色器，用來計算畫面上的畫素該是什麼顏色
const char* FragmentShaderSource = R"(
	#version 460 core

	layout (location = 0) in  vec3 ourColor;         // 接收 VertexShaderSource 傳送來的 ourColor
	layout (location = 0) out vec4 FragColor;        // 要畫到(輸出)螢幕上的顏色
	layout (location = 0) uniform float timeOffset;  // 外部通過 glProgramUniform1f 所傳遞進來的數值，location 數字必須跟 glProgramUniform1f 的第二個參數相符

	void main()
	{
		FragColor = vec4( ourColor.r, ourColor.g * sin(timeOffset), ourColor.b, 1.0f );
	}
)";

// 三角形數據，描述三個點的位置與顏色
constexpr float Vertices[] = {
	// 座標              // 顏色
	 0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f,    // 頂部 (紅)
	 0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,    // 右下 (綠)
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

	constexpr GLuint BINDING_POINT          = 0;   // VAO 的綁定點編號，這個數字不會被 shader 使用到，shader 只會使用 location 編號
	constexpr GLuint VERTEX_ATTRIB_POSITION = 0;   // shader 裡的 layout(location = 0) 對應到這個數字
	constexpr GLuint VERTEX_ATTRIB_COLOR    = 1;   // shader 裡的 layout(location = 1) 對應到這個數字

	GLuint   VAO = 0;   // 用來申請 Vertex Array Object，裡面會記錄 VBO 的綁定點、屬性格式、屬性綁定點等資訊
	GLuint   VBO = 0;   // 用來申請 Vertex Buffer Object，裡面會存放 vertex 數據資料

	glCreateVertexArrays( 1, &VAO );
	glCreateBuffers( 1, &VBO );

	// 配置緩衝區空間並儲存數據，固定式存儲(Immutable Storage)，效能更好
	// 最後一個參數設為 0，表示這個緩衝區的內容不會被修改
	glNamedBufferStorage( VBO, sizeof( Vertices ), Vertices, 0 );

	// 1. 將 VBO 綁定到 VAO 的「第 0 號綁定點」，並規定 6 個 float 為一組 vertex 資料
	glVertexArrayVertexBuffer( VAO,
		BINDING_POINT,
		VBO,
		0,    // 從 VBO 的第 0 個 byte 開始讀取
		6 * sizeof( float ) );

	// 2. 啟用 vertex 屬性欄位，數字自己定義，將 shader 裡的 layout(location = ?) 數字也寫一樣的即可
	glEnableVertexArrayAttrib( VAO, VERTEX_ATTRIB_POSITION ); // 啟用 location 0，座標
	glEnableVertexArrayAttrib( VAO, VERTEX_ATTRIB_COLOR );    // 啟用 location 1，顏色

	// 3. 設定屬性格式
	glVertexArrayAttribFormat( VAO, VERTEX_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 0 ); // 偏移 0
	glVertexArrayAttribFormat( VAO, VERTEX_ATTRIB_COLOR, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ) ); // 偏移 3 個 float

	// 4. 告訴 VAO，屬性 0 和 1 都要去「第 0 號綁定點」拿資料
	glVertexArrayAttribBinding( VAO, VERTEX_ATTRIB_POSITION, BINDING_POINT );  // 屬性 0
	glVertexArrayAttribBinding( VAO, VERTEX_ATTRIB_COLOR,    BINDING_POINT );  // 屬性 1

	//--------------------------------------------------------------------------

	bool       quit = false;
	SDL_Event  event;

	/*
	 * 這迴圈的工作就兩件事：
	 * 1. 處理來自作業系統的 event
	 * 2. 描繪視窗上的畫面
	 */
	while ( false == quit )
	{
		// 處理來自作業系統的 event
		while ( SDL_PollEvent( &event ) != 0 )
		{
			// 目前只在乎使用者按下視窗關閉鈕而已
			if ( event.type == SDL_QUIT ) quit = true;
		}

		gl::ClearScreen();

		const float timeValue = sdl::GetTick();

		// 將 timeValue 輸入進 FragmentShaderSource 內的 timeOffset
		glProgramUniform1f(
			myShader.getID(),
			0,   // 數字 0 對應著 uniform 變數的 location，跟 VERTEX_ATTRIB_POSITION 的數字無關
			timeValue );

		myShader.use();                       // 趕在開始描繪之前綁定 shader
		glBindVertexArray( VAO );             // 趕在開始描繪之前綁定 VAO
		glDrawArrays( GL_TRIANGLES, 0, 3 );   // 以 GL_TRIANGLES 的規則來進行描繪，會使用之前已經設定好的數據資料

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
