#include <cstring>
#include <cstdint>
#include <fmt/core.h>
#include <glad/glad.h>
#include <SDL.h>
#include "sdl/Window.hpp"
#include "gl/ShaderProgram.hpp"
#include "gl/StreamingBuffer.hpp"
#include "gl/VertexArray.hpp"
#include "gl/Camera.hpp"
#include "gl/CreateImage.hpp"
#include "gl/TextureManager.hpp"

namespace{

/*
 * aOffsetScale / aLayerIndex 為 instanced 屬性
 * 各自代表每個四邊形的世界座標偏移、縮放
 * 以及要取樣的 texture array layer
 */
const char* VertexShaderSource = R"(
	#version 460 core
	#extension GL_ARB_bindless_texture : require

	layout (location = 0) in vec2 aPos;
	layout (location = 1) in vec2 aUV;
	layout (location = 2) in vec4 aOffsetScale; // xyz = 世界座標偏移, w = 縮放
	layout (location = 3) in int  aLayerIndex;

	layout (location = 0) out vec2 vUV;
	layout (location = 1) flat out int vLayer;

	// 使用 Camera 傳來的 View / Projection 矩陣（std140 UBO）
	layout (std140, binding = 0) uniform CameraBlock
	{
		mat4 view;
		mat4 projection;
		mat4 viewProjection;
	} camera;

	void main()
	{
		vec3 worldPos = vec3( aPos * aOffsetScale.w, 0.0 ) + aOffsetScale.xyz;
		gl_Position = camera.viewProjection * vec4( worldPos, 1.0 );

		vUV    = aUV;
		vLayer = aLayerIndex;
	}
)";

// 透過 Bindless Texture Handle 存取 texture array
// 不需要在 CPU 端呼叫 glBindTexture
const char* FragmentShaderSource = R"(
	#version 460 core
	#extension GL_ARB_bindless_texture : require

	layout (location = 0) in  vec2 vUV;
	layout (location = 1) flat in int vLayer;
	layout (location = 0) out vec4 FragColor;

	layout (bindless_sampler) uniform sampler2DArray texArray;

	void main()
	{
		FragColor = texture( texArray, vec3( vUV, float( vLayer ) ) );
	}
)";

// 一個共用的單位四邊形（-0.5 ~ 0.5），position + uv
constexpr float QuadVertices[] = {
	// pos            // uv
	-0.5f,  0.5f,      0.0f, 1.0f,
	 0.5f,  0.5f,      1.0f, 1.0f,
	 0.5f, -0.5f,      1.0f, 0.0f,
	-0.5f, -0.5f,      0.0f, 0.0f,
};

constexpr GLuint QuadIndices[] = { 0, 1, 2, 2, 3, 0 };

constexpr size_t QuadVertexStride = 4 * sizeof( float );

// 每個 instance（四邊形）各自的資料：世界座標偏移 + 縮放 + texture layer
struct InstanceData
{
	glm::vec3 offset;
	float     scale;
	int       layerIndex;
};

constexpr size_t InstanceStride = sizeof( InstanceData );

// 對應 glMultiDrawElementsIndirect 所需的 command 結構
struct DrawElementsIndirectCommand
{
	GLuint count;
	GLuint instanceCount;
	GLuint firstIndex;
	GLint  baseVertex;
	GLuint baseInstance;
};

constexpr int WindowWidth  = 800;
constexpr int WindowHeight = 600;
constexpr int ImageCount   = 4;   // 本範例要顯示的圖片數量

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
	sdl::Window app;

	if ( false == app.init( "sample 03", WindowWidth, WindowHeight ) )
	{
		fmt::print( "視窗建立失敗\n" );
		return EXIT_FAILURE;
	}

	//--------------------------------------------------------------------------

	gl::ShaderProgram   myShader( VertexShaderSource, FragmentShaderSource );

	//--------------------------------------------------------------------------

	gl::TextureManager  textureManager;

	// 建立 4 張圖片的 texture array + bindless handle
	if ( !textureManager.build( { "brick", "grass", "sand", "water" } ) )
	{
		fmt::print( "TextureManager 建立失敗\n" );
		return EXIT_FAILURE;
	}

	// 把 bindless handle 傳給 fragment shader 的 sampler2DArray（全域只需設定一次）
	{
		const GLint texArrayLoc = glGetUniformLocation( myShader.getID(), "texArray" );
		glProgramUniformHandleui64ARB( myShader.getID(), texArrayLoc, textureManager.getHandle() );
	}

	//--------------------------------------------------------------------------

	gl::VertexArray VAO;

	GLuint quadVBO = 0;
	GLuint quadEBO = 0;

	glCreateBuffers( 1, &quadVBO );
	glNamedBufferStorage( quadVBO, sizeof( QuadVertices ), QuadVertices, 0 );

	glCreateBuffers( 1, &quadEBO );
	glNamedBufferStorage( quadEBO, sizeof( QuadIndices ), QuadIndices, 0 );

	VAO.bindElementBuffer( quadEBO );
	VAO.bindVertexBuffer( 0, quadVBO, 0, static_cast<GLsizei>( QuadVertexStride ) );

	// binding 0：共用四邊形頂點（pos + uv）
	VAO.enableAttrib( 0 );
	VAO.enableAttrib( 1 );
	VAO.setAttribFormat( 0, 2, GL_FLOAT, GL_FALSE, 0 );
	VAO.setAttribFormat( 1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof( float ) );
	VAO.setAttribBinding( 0, 0 );
	VAO.setAttribBinding( 1, 0 );

	// binding 1：instanced 屬性（每個 instance 各自的偏移/縮放/layer）
	VAO.enableAttrib( 2 );
	VAO.enableAttrib( 3 );
	VAO.setAttribFormat( 2, 4, GL_FLOAT, GL_FALSE, offsetof( InstanceData, offset ) );
	VAO.setAttribIFormat( 3, 1, GL_INT, offsetof( InstanceData, layerIndex ) );
	VAO.setAttribBinding( 2, 1 );
	VAO.setAttribBinding( 3, 1 );
	VAO.setBindingDivisor( 1, 1 ); // 每個 instance 更新一次

	//--------------------------------------------------------------------------

	gl::StreamingBuffer cameraBuffer( GL_UNIFORM_BUFFER, sizeof( gl::Camera::UniformBlock ), 3 );
	gl::StreamingBuffer instanceBuffer( GL_ARRAY_BUFFER, sizeof( InstanceData ) * ImageCount, 3 );
	gl::StreamingBuffer indirectBuffer( GL_DRAW_INDIRECT_BUFFER, sizeof( DrawElementsIndirectCommand ) * ImageCount, 3 );

	if ( !cameraBuffer.isValid() || !instanceBuffer.isValid() || !indirectBuffer.isValid() )
	{
		fmt::print( "StreamingBuffer 建立失敗（驅動可能不支援 ARB_buffer_storage）\n" );
		return EXIT_FAILURE;
	}

	//--------------------------------------------------------------------------

	// 建立攝影機，往 +Z 退開才能看到排列在 Z = 0 平面上的四張圖片
	gl::Camera camera( glm::vec3( 0.0f, 0.0f, 3.0f ) );

	constexpr GLuint CameraBindingIndex = 0;

	// 四張圖片排成 2x2 網格，各自對應 texture array 的 layer 0~3
	constexpr glm::vec3 QuadOffsets[ImageCount] = {
		{ -0.6f,  0.6f, 0.0f },
		{  0.6f,  0.6f, 0.0f },
		{ -0.6f, -0.6f, 0.0f },
		{  0.6f, -0.6f, 0.0f },
	};

	constexpr float QuadScale = 1.0f;
	bool            quit     = false;
	SDL_Event       msg;
	Uint32          lastTick = SDL_GetTicks();

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

		// 非同步上傳 Camera 矩陣至 UBO，並綁定到 binding = 0
		const float aspectRatio = static_cast<float>( WindowWidth ) / static_cast<float>( WindowHeight );
		camera.uploadToUBO( cameraBuffer, aspectRatio, CameraBindingIndex );

		// 非同步寫入 4 個 instance 的資料（位置 / 縮放 / layer index）
		void* instanceDst = instanceBuffer.beginWrite();

		if ( instanceDst != nullptr )
		{
			InstanceData instances[ImageCount];

			for ( int i = 0; i < ImageCount; ++i )
			{
				instances[i].offset     = QuadOffsets[i];
				instances[i].scale      = QuadScale;
				instances[i].layerIndex = i;
			}

			std::memcpy( instanceDst, instances, sizeof( instances ) );

			VAO.bindVertexBuffer(
				1,
				instanceBuffer.getBufferId(),
				instanceBuffer.getCurrentOffset(),
				static_cast<GLsizei>( InstanceStride ) );

			instanceBuffer.endWrite();

			// 非同步寫入 Indirect Draw Command：
			// 每張圖片各自一筆 command，instanceCount = 1，
			// baseInstance = i 讓 instanced 屬性剛好對應到第 i 筆 InstanceData
			void* indirectDst = indirectBuffer.beginWrite();

			if ( indirectDst != nullptr )
			{
				DrawElementsIndirectCommand commands[ImageCount];

				for ( GLuint i = 0; i < ImageCount; ++i )
				{
					commands[i].count         = static_cast<GLuint>( std::size( QuadIndices ) );
					commands[i].instanceCount = 1;
					commands[i].firstIndex    = 0;
					commands[i].baseVertex    = 0;
					commands[i].baseInstance  = i;
				}

				std::memcpy( indirectDst, commands, sizeof( commands ) );

				myShader.use();
				VAO.bind();

				glBindBuffer( GL_DRAW_INDIRECT_BUFFER, indirectBuffer.getBufferId() );

				// 一次呼叫，透過 indirect buffer 描述的 4 筆 command
				// 分別繪製 4 張圖片（各自對應不同的 texture array layer）
				glMultiDrawElementsIndirect(
					GL_TRIANGLES,
					GL_UNSIGNED_INT,
					reinterpret_cast<const void*>( indirectBuffer.getCurrentOffset() ),
					ImageCount,
					sizeof( DrawElementsIndirectCommand ) );

				indirectBuffer.endWrite();
			}
		}

		app.refresh();
	}

	//--------------------------------------------------------------------------

	glDeleteBuffers( 1, &quadVBO );
	glDeleteBuffers( 1, &quadEBO );

	myShader.release();

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
