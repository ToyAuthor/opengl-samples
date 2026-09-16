#include <cstring>
#include <fmt/core.h>
#include <glad/glad.h>
#include <SDL.h>
#include "sdl/Window.hpp"
#include "gl/ShaderProgram.hpp"
#include "gl/StreamingBuffer.hpp"
#include "gl/VertexArray.hpp"
#include "gl/Camera.hpp"

namespace{

// 頂點著色器：多加一個 std140 UBO，接收 Camera 傳來的 View / Projection 矩陣
const char* VertexShaderSource = R"(
	#version 460 core

	layout (location = 0) in vec3 aPos;
	layout (location = 1) in vec3 aColor;
	layout (location = 0) out vec3 ourColor;

	layout (std140, binding = 0) uniform CameraBlock
	{
		mat4 view;
		mat4 projection;
		mat4 viewProjection;
	} camera;

	void main()
	{
		gl_Position = camera.viewProjection * vec4( aPos, 1.0 );
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

constexpr size_t VertexStride = 6 * sizeof( float );

constexpr int WindowWidth  = 800;
constexpr int WindowHeight = 600;

// 依照 Camera::Movement 對應鍵盤按鍵，統一在此處理輸入
void handleKeyboardInput( gl::Camera& camera, float deltaTime )
{
	const Uint8* state = SDL_GetKeyboardState( nullptr );

	if ( state[ SDL_SCANCODE_W ] ) camera.processKeyboard( gl::Camera::Movement::Forward,  deltaTime );
	if ( state[ SDL_SCANCODE_S ] ) camera.processKeyboard( gl::Camera::Movement::Backward, deltaTime );
	if ( state[ SDL_SCANCODE_A ] ) camera.processKeyboard( gl::Camera::Movement::Left,     deltaTime );
	if ( state[ SDL_SCANCODE_D ] ) camera.processKeyboard( gl::Camera::Movement::Right,    deltaTime );
	if ( state[ SDL_SCANCODE_E ] ) camera.processKeyboard( gl::Camera::Movement::Up,       deltaTime );
	if ( state[ SDL_SCANCODE_Q ] ) camera.processKeyboard( gl::Camera::Movement::Down,     deltaTime );
}

int main2()
{
	sdl::Window app;   // 使用 SDL2 來建立視窗

	if ( false == app.init( "sample 01", WindowWidth, WindowHeight ) )
	{
		fmt::print( "視窗建立失敗\n" );
		return EXIT_FAILURE;
	}

	gl::ShaderProgram   myShader( VertexShaderSource, FragmentShaderSource );

	//--------------------------------------------------------------------------

	// 建立 VAO（改用 gl::VertexArray 包裝，RAII 自動管理生命週期）
	gl::VertexArray VAO;

	// VBO 改由 StreamingBuffer 管理，使用 Persistent Mapping + ring buffer
	// 讓 CPU 端可以非同步地寫入頂點資料，不需等待前一幀的 GPU 讀取完成
	gl::StreamingBuffer vertexBuffer( GL_ARRAY_BUFFER, sizeof( Vertices ), 3 );

	// Camera 的 View / Projection 矩陣也用同樣手法建立 UBO，
	// 每幀非同步上傳，不需等待 GPU 完成上一幀的 draw call 才能寫入
	gl::StreamingBuffer cameraBuffer( GL_UNIFORM_BUFFER, sizeof( gl::Camera::UniformBlock ), 3 );

	if ( !vertexBuffer.isValid() || !cameraBuffer.isValid() )
	{
		fmt::print( "StreamingBuffer 建立失敗（驅動可能不支援 ARB_buffer_storage）\n" );
		return EXIT_FAILURE;
	}

	// 啟用頂點屬性 0 (位置) 與 1 (顏色)
	VAO.enableAttrib( 0 );
	VAO.enableAttrib( 1 );

	// 設定屬性格式
	VAO.setAttribFormat( 0, 3, GL_FLOAT, GL_FALSE, 0 );
	VAO.setAttribFormat( 1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ) );

	// 告訴 VAO，屬性 0 和 1 都要去「第 0 號綁定點」拿資料
	VAO.setAttribBinding( 0, 0 );
	VAO.setAttribBinding( 1, 0 );

	// 建立攝影機，初始位置往 +Z 退開，才看得到三角形（三角形位於 Z = 0）
	gl::Camera camera( glm::vec3( 0.0f, 0.0f, 3.0f ) );

	constexpr GLuint CameraBindingIndex = 0;

	//--------------------------------------------------------------------------

	bool       quit     = false;
	SDL_Event  msg;
	Uint32     lastTick = SDL_GetTicks();

	/*
	 * 這迴圈的工作就兩件事：
	 * 1. 處理來自作業系統的 event
	 * 2. 描繪視窗上的畫面
	 */
	while ( false == quit )
	{
		const Uint32 currentTick = SDL_GetTicks();
		const float  deltaTime   = static_cast<float>( currentTick - lastTick ) / 1000.0f;
		lastTick = currentTick;

		// 處理來自作業系統的 event
		while ( SDL_PollEvent( &msg ) != 0 )
		{
			if ( msg.type == SDL_QUIT ) quit = true;

			if ( msg.type == SDL_MOUSEMOTION )
			{
				camera.processMouseMovement(
					static_cast<float>( msg.motion.xrel ),
					static_cast<float>( -msg.motion.yrel ) );
			}
		}

		handleKeyboardInput( camera, deltaTime );

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

		// 非同步上傳 Camera 矩陣至 UBO，並綁定到 binding = 0
		const float aspectRatio = static_cast<float>( WindowWidth ) / static_cast<float>( WindowHeight );
		camera.uploadToUBO( cameraBuffer, aspectRatio, CameraBindingIndex );

		// 非同步寫入頂點資料：
		// beginWrite() 會等待「同一個 ring 槽位」上一次使用它的 GPU 指令執行完畢，
		// 由於 ringCount = 3，通常不會真的卡住 CPU
		void* dst = vertexBuffer.beginWrite();

		if ( dst != nullptr )
		{
			std::memcpy( dst, Vertices, sizeof( Vertices ) );

			// 將 VAO 的第 0 號綁定點指向目前這個槽位的 buffer + offset
			VAO.bindVertexBuffer(
				0,
				vertexBuffer.getBufferId(),
				vertexBuffer.getCurrentOffset(),
				static_cast<GLsizei>( VertexStride ) );

			myShader.use(); // 只有在真正要 Draw 的時候才 Bind Program

			// 渲染時只需要綁定 VAO 即可
			VAO.bind();
			glDrawArrays( GL_TRIANGLES, 0, 3 );

			// 插入 fence 標記「這個槽位」目前這次 Draw 已提交，
			// 並切換到下一個槽位供下一幀使用
			vertexBuffer.endWrite();
		}

		/*
		 * 更新螢幕
		 * 如果你的螢幕裝置是 60 赫茲更新率的話
		 * 這裡就要花費 1/60 秒來更新視窗畫面
		 */
		app.refresh();
	}

	//--------------------------------------------------------------------------

	myShader.release();

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
