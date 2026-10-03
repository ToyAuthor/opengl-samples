/*
 * 範例名稱：sample03
 * -----------------------------------------------------------------------------
 * 用非同步方式將 vertex 資料、矩陣、texture(PBO)傳送至顯卡
 * 非同步是避免畫面卡頓的關鍵，讓 CPU 與 GPU 不用互相等待
 * 在 gl::StreamingBuffer 當中已經很好的實作了這個功能
 *
 * 使用 FPS 遊戲方式操作視角
 * 採用 UBO 來傳送 Camera 的 View / Projection 矩陣
 */

#include <cstring>
#include <string>
#include <memory>
#include <fmt/core.h>
#include <glad/glad.h>
#include <SDL.h>
#include "sdl/Utils.hpp"
#include "sdl/Window.hpp"
#include "gl/Core.hpp"
#include "gl/ShaderProgram.hpp"
#include "gl/Camera.hpp"           // 提供一個簡單的 FPS 攝影機，並提供 std140 UBO 的 UniformBlock 結構
#include "gl/StreamingBuffer.hpp"  // 用來建立一個 CPU 與 GPU 之間的 Persistent Mapping buffer，讓 CPU 可以非同步地寫入資料到 GPU
#include "gl/VertexArray.hpp"      // 用來建立 VAO，並管理 VertexAttrib 與 VertexBinding
#include "gl/VertexAttrib.hpp"
#include "gl/VertexBuffer.hpp"
#include "gl/ImageData.hpp"
#include "gl/CreateImage.hpp"

namespace{

// vertex 著色器：多加一個 std140 UBO，接收 Camera 傳來的 View / Projection 矩陣
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

// 貼圖四邊形使用的 shader：純粹把 texture 內容畫出來，
// 用來驗證 PBO 非同步上傳的結果是否正確
const char* TexVertexShaderSource = R"(
	#version 460 core

	layout (location = 0) in vec3 aPos;
	layout (location = 1) in vec2 aUV;
	layout (location = 0) out vec2 vUV;

	layout (std140, binding = 0) uniform CameraBlock
	{
		mat4 view;
		mat4 projection;
		mat4 viewProjection;
	} camera;

	void main()
	{
		gl_Position = camera.viewProjection * vec4( aPos, 1.0 );
		vUV = aUV;
	}
)";

const char* TexFragmentShaderSource = R"(
	#version 460 core

	layout (location = 0) in  vec2 vUV;
	layout (location = 0) out vec4 FragColor;
	layout (binding = 0) uniform sampler2D myTexture;

	void main()
	{
		FragColor = texture( myTexture, vUV );
	}
)";

// 三角形數據
constexpr float Vertices[] = {
	 // 位置             // 顏色
	 0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f,    // 頂部 (紅)
	 0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,    // 右下 (綠)
	-0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f     // 左下 (藍)
};

constexpr size_t VertexStride = 6 * sizeof( float );

// 貼圖用的四邊形，位置放在三角形右側，避免重疊
constexpr float TexQuadVertices[] = {
	// 位置               // UV
	 0.8f,  0.6f, 0.0f,   0.0f, 1.0f,
	 1.6f,  0.6f, 0.0f,   1.0f, 1.0f,
	 1.6f, -0.2f, 0.0f,   1.0f, 0.0f,

	 0.8f,  0.6f, 0.0f,   0.0f, 1.0f,
	 1.6f, -0.2f, 0.0f,   1.0f, 0.0f,
	 0.8f, -0.2f, 0.0f,   0.0f, 0.0f,
};

constexpr size_t TexQuadVertexStride = 5 * sizeof( float );

constexpr int WindowWidth  = 800;
constexpr int WindowHeight = 600;

constexpr int TextureWidth  = 128;
constexpr int TextureHeight = 128;

// 依照 Camera::Movement 對應鍵盤按鍵，統一在此處理輸入
void HandleKeyboardInput( gl::Camera& camera, float deltaTime )
{
	const Uint8* state = SDL_GetKeyboardState( nullptr );

	if ( state[ SDL_SCANCODE_W ] ) camera.processKeyboard( gl::Camera::Movement::Forward,  deltaTime );
	if ( state[ SDL_SCANCODE_S ] ) camera.processKeyboard( gl::Camera::Movement::Backward, deltaTime );
	if ( state[ SDL_SCANCODE_A ] ) camera.processKeyboard( gl::Camera::Movement::Left,     deltaTime );
	if ( state[ SDL_SCANCODE_D ] ) camera.processKeyboard( gl::Camera::Movement::Right,    deltaTime );
	if ( state[ SDL_SCANCODE_E ] ) camera.processKeyboard( gl::Camera::Movement::Up,       deltaTime );
	if ( state[ SDL_SCANCODE_Q ] ) camera.processKeyboard( gl::Camera::Movement::Down,     deltaTime );
}

// 依照 tick 產生一張「會捲動」的棋盤格圖片，
// 用來證明每一幀透過 PBO 上傳的貼圖內容確實有在更新
void FillScrollingCheckerImage( gl::ImageData& img, int scrollOffset )
{
	for ( int y = 0; y < img.height; ++y )
	{
		for ( int x = 0; x < img.width; ++x )
		{
			const int  sx      = ( x + scrollOffset ) % img.width;
			const bool checker = ( ( sx / 16 ) + ( y / 16 ) ) % 2 == 0;
			const int  i       = ( y * img.width + x ) * img.channels;

			img.pixels[i + 0] = checker ? 255 : 40;
			img.pixels[i + 1] = checker ? 200 : 40;
			img.pixels[i + 2] = checker ? 60  : 200;
			img.pixels[i + 3] = 255;
		}
	}
}

int main2( sdl::Window &app, std::shared_ptr<gl::Core> core )
{
	if ( false == app.init( "sample 03", WindowWidth, WindowHeight ) )
	{
		fmt::print( "視窗建立失敗\n" );
		return EXIT_FAILURE;
	}

	gl::ShaderProgram   myShader( core, VertexShaderSource, FragmentShaderSource );
	gl::ShaderProgram   texShader( core, TexVertexShaderSource, TexFragmentShaderSource );

	//--------------------------------------------------------------------------

	// 建立 VAO（改用 gl::VertexArray 包裝，RAII 自動管理生命週期）
	auto VAO = std::make_shared<gl::VertexArray>( core );

	// 啟用 vertex 屬性 0 (位置) 與 1 (顏色)
	auto attrib_0 = std::make_shared<gl::VertexAttrib>( VAO, 0 );  // 位置(location = 0)
	auto attrib_1 = std::make_shared<gl::VertexAttrib>( VAO, 1 );  // 顏色(location = 1)

	// 設定屬性格式
	attrib_0->setFormat( 3, GL_FLOAT, GL_FALSE, 0 );
	attrib_1->setFormat( 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ) );

	// 將屬性 0 和 1 都黏到綁定點
	VAO->bindingIndex0.bind( attrib_0 );
	VAO->bindingIndex0.bind( attrib_1 );

	// VBO 改由 StreamingBuffer 管理，使用 Persistent Mapping + ring buffer
	// 讓 CPU 端可以非同步地寫入頂點資料，不需等待前一幀的 GPU 讀取完成
	gl::StreamingBuffer vertexBuffer( GL_ARRAY_BUFFER, sizeof( Vertices ), 3 );

	// Camera 的 View / Projection 矩陣也用同樣手法建立 UBO，
	// 每幀非同步上傳，不需等待 GPU 完成上一幀的 draw call 才能寫入
	gl::StreamingBuffer cameraBuffer( GL_UNIFORM_BUFFER, sizeof( gl::Camera::UniformBlock ), 3 );

	if ( !vertexBuffer.isValid() || !cameraBuffer.isValid() )
	{
		fmt::print( "StreamingBuffer 建立失敗(驅動可能不支援 ARB_buffer_storage)\n" );
		return EXIT_FAILURE;
	}

	//--------------------------------------------------------------------------
	// 貼圖四邊形所需的 VAO / VBO（vertex 資料量不大且不需每幀變動，直接用靜態 VBO 即可）

	auto texVAO = std::make_shared<gl::VertexArray>( core );
	auto texVBO = std::make_shared<gl::VertexBuffer>( sizeof( TexQuadVertices ), TexQuadVertices );

	auto texAttrib_0 = std::make_shared<gl::VertexAttrib>( texVAO, 0 );  // 位置
	auto texAttrib_1 = std::make_shared<gl::VertexAttrib>( texVAO, 1 );  // UV

	texAttrib_0->setFormat( 3, GL_FLOAT, GL_FALSE, 0 );
	texAttrib_1->setFormat( 2, GL_FLOAT, GL_FALSE, 3 * sizeof( float ) );

	texVAO->bindingIndex0.bind( texAttrib_0 );
	texVAO->bindingIndex0.bind( texAttrib_1 );
	texVAO->bindingIndex0.bindVBO( texVBO->getID(), 0, static_cast<GLsizei>( TexQuadVertexStride ) );

	//--------------------------------------------------------------------------
	// 建立 texture 本體（Immutable Storage），初始內容用 gl::CreateImage 產生

	gl::ImageData initialImage;
	initialImage.width  = TextureWidth;
	initialImage.height = TextureHeight;
	gl::CreateImage( "sample03", initialImage );

	GLuint textureId = 0;
	glCreateTextures( GL_TEXTURE_2D, 1, &textureId );
	glTextureStorage2D( textureId, 1, GL_RGBA8, TextureWidth, TextureHeight );
	glTextureSubImage2D(
		textureId, 0, 0, 0,
		TextureWidth, TextureHeight,
		GL_RGBA, GL_UNSIGNED_BYTE,
		initialImage.pixels.data() );

	glTextureParameteri( textureId, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTextureParameteri( textureId, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTextureParameteri( textureId, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTextureParameteri( textureId, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	// PBO(Pixel Unpack Buffer)：跟 vertexBuffer / cameraBuffer 一樣，
	// 借用 gl::StreamingBuffer 的 Persistent Mapping + ring buffer 機制，
	// 讓 CPU 每幀寫入圖片資料時不需要等待 GPU 完成前一次的 texture 上傳
	gl::StreamingBuffer texturePBO(
		GL_PIXEL_UNPACK_BUFFER,
		static_cast<size_t>( TextureWidth ) * TextureHeight * gl::ImageData::channels,
		3 );

	if ( !texturePBO.isValid() )
	{
		fmt::print( "Texture PBO 建立失敗(驅動可能不支援 ARB_buffer_storage)\n" );
		return EXIT_FAILURE;
	}

	// CPU 端用來產生每幀圖片內容的暫存資料
	gl::ImageData scrollingImage;
	scrollingImage.width  = TextureWidth;
	scrollingImage.height = TextureHeight;
	scrollingImage.pixels.resize( static_cast<size_t>( TextureWidth ) * TextureHeight * gl::ImageData::channels );

	//--------------------------------------------------------------------------

	// 建立攝影機，初始位置往 +Z 退開，才看得到三角形（三角形位於 Z = 0）
	gl::Camera camera( glm::vec3( 0.0f, 0.0f, 3.0f ) );

	bool       quit         = false;
	SDL_Event  event;
	float      lastTick     = sdl::GetTick();
	int        scrollOffset = 0;

	/*
	 * 這迴圈的工作就兩件事：
	 * 1. 處理來自作業系統的 event
	 * 2. 描繪視窗上的畫面
	 */
	while ( false == quit )
	{
		const float currentTick = sdl::GetTick();
		const float deltaTime   = currentTick - lastTick;
		lastTick = currentTick;

		// 處理來自作業系統的 event
		while ( SDL_PollEvent( &event ) != 0 )
		{
			switch ( event.type )
			{
				case SDL_QUIT:
					quit = true;
					break;
				case SDL_MOUSEMOTION:
					camera.processMouseMovement(
						static_cast<float>(  event.motion.xrel ),
						static_cast<float>( -event.motion.yrel ) );
					break;
				default:
					break;
			}
		}

		HandleKeyboardInput( camera, deltaTime );

		core->clear();

		// 更新參數：glProgramUniform 不需要先 glUseProgram
		const float timeValue = sdl::GetTick();

		// 將 timeValue 傳送進 fragmentShaderSource 內的 timeOffset
		glProgramUniform1f( myShader.getID(), 0, timeValue );

		// 非同步上傳 Camera 矩陣至 UBO，並綁定到 binding = 0
		const float aspectRatio = static_cast<float>( WindowWidth ) / static_cast<float>( WindowHeight );
		camera.uploadToUBO( cameraBuffer, aspectRatio, 0 );

		// 非同步寫入頂點資料：
		// beginWrite() 會等待「同一個 ring 槽位」上一次使用它的 GPU 指令執行完畢，
		// 理想是幾乎不用等，因為 ring buffer 有多個槽位可以輪替使用
		void* dst = vertexBuffer.beginWrite();

		if ( dst != nullptr )
		{
			// 這邊是可以修改 vertex 資訊的，然後更新到 GPU
			std::memcpy( dst, Vertices, sizeof( Vertices ) );

			// 將 VBO 目前槽位的 buffer + offset 黏到綁定點
			VAO->bindingIndex0.bindVBO(
				vertexBuffer.getBufferId(),
				vertexBuffer.getCurrentOffset(),
				static_cast<GLsizei>( VertexStride ) );

			myShader.use();
			VAO->bind();
			glDrawArrays( GL_TRIANGLES, 0, 3 );

			// 插入 fence 標記「這個槽位」目前這次 Draw 已提交，
			// 並切換到下一個槽位供下一幀使用
			vertexBuffer.endWrite();
		}

		//----------------------------------------------------------------------
		// PBO 非同步上傳 texture 示範：
		// 1. CPU 端在 mapped 記憶體上產生本幀的圖片資料(捲動棋盤格)
		// 2. 綁定 PBO 至 GL_PIXEL_UNPACK_BUFFER
		// 3. glTextureSubImage2D 的最後一個參數改傳「offset」而非 CPU 指標，
		//    驅動會知道資料已經在 GPU 可存取的 buffer 裡，直接 DMA 過去，
		//    不需要 CPU 端阻塞等待資料傳輸完成
		void* pboDst = texturePBO.beginWrite();

		if ( pboDst != nullptr )
		{
			scrollOffset = ( scrollOffset + 1 ) % TextureWidth;

			FillScrollingCheckerImage( scrollingImage, scrollOffset );
			std::memcpy( pboDst, scrollingImage.pixels.data(), scrollingImage.pixels.size() );

			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, texturePBO.getBufferId() );

			glTextureSubImage2D(
				textureId, 0, 0, 0,
				TextureWidth, TextureHeight,
				GL_RGBA, GL_UNSIGNED_BYTE,
				reinterpret_cast<const void*>( texturePBO.getCurrentOffset() ) );

			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );

			// 插入 fence 並切換到下一個槽位，讓下一幀可以非同步寫入而不互相衝突
			texturePBO.endWrite();

			glBindTextureUnit( 0, textureId );

			texShader.use();
			texVAO->bind();
			glDrawArrays( GL_TRIANGLES, 0, 6 );
		}

		app.refresh();
	}

	//--------------------------------------------------------------------------

	glDeleteTextures( 1, &textureId );

	myShader.release();
	texShader.release();

	return EXIT_SUCCESS;
}

}

#undef main
int main()
{
	int result = EXIT_FAILURE;

	try
	{
		fmt::print( "執行程式\n" );

		sdl::Window   app;
		auto          core = std::make_shared<gl::Core>();

		result = main2( app, core );
	}
	catch ( const std::exception& e )
	{
		fmt::print( "異常訊息：{}\n", e.what() );
	}
	catch ( ... )
	{
		fmt::print( "捕獲到未知的異常\n" );
	}

	return result;
}
