#include <cstring>
#include <cstdint>
#include <fmt/core.h>
#include <glad/glad.h>
#include <SDL.h>
#include "sdl/Utils.hpp"
#include "sdl/Window.hpp"
#include "gl/Utils.hpp"
#include "gl/ShaderProgram.hpp"
#include "gl/StreamingBuffer.hpp"
#include "gl/VertexArray.hpp"
#include "gl/VertexBinding.hpp"
#include "gl/VertexAttrib.hpp"
#include "gl/VertexBuffer.hpp"
#include "gl/ElementsBuffer.hpp"
#include "gl/Camera.hpp"
#include "gl/CreateImage.hpp"
#include "gl/TextureManager.hpp"

namespace{

/*
 * aOffsetScale / aLayerIndex / aMaterialIndex 為 instanced 屬性
 * 各自代表每個四邊形的世界座標偏移、縮放、
 * 要取樣的 texture array layer，
 * 以及要使用哪一組 texture array(對應 SSBO 中的 handle 索引)
 */
const char* VertexShaderSource = R"(
	#version 460 core
	#extension GL_ARB_bindless_texture : require  // 允許在 shader 中使用 bindless texture

	layout (location = 0) in vec2 aPos;
	layout (location = 1) in vec2 aUV;
	layout (location = 2) in vec4 aOffsetScale;   // xyz = 世界座標偏移, w = 縮放
	layout (location = 3) in int  aLayerIndex;
	layout (location = 4) in int  aMaterialIndex; // 對應 SSBO 中第幾組 texture array

	layout (location = 0) out vec2 vUV;
	layout (location = 1) flat out int vLayer;
	layout (location = 2) flat out int vMaterial;

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

		vUV       = aUV;
		vLayer    = aLayerIndex;
		vMaterial = aMaterialIndex;
	}
)";

/*
 * bindless handle 陣列改存在 SSBO 中(std430)
 * ARB_bindless_texture 允許把 sampler 型別直接放進 buffer block
 * 因為 sampler 底層本質上就是 64-bit 的 handle
 * 這是用來管理大量材質 handle 的做法
 * 不需要每個材質各自佔一個 uniform location
 */
const char* FragmentShaderSource = R"(
	#version 460 core
	#extension GL_ARB_bindless_texture : require

	layout (location = 0) in  vec2 vUV;
	layout (location = 1) flat in int vLayer;
	layout (location = 2) flat in int vMaterial;
	layout (location = 0) out vec4 FragColor;

	layout (std430, binding = 1) readonly buffer TextureBlock
	{
		sampler2DArray texArrays[];
	};

	void main()
	{
		FragColor = texture( texArrays[ vMaterial ], vec3( vUV, float( vLayer ) ) );
	}
)";

// 一個共用的單位四邊形(-0.5 ~ 0.5)，位置 + UV(texture座標)
constexpr float QuadVertices[] = {
	// 位置         // UV
	-0.5f,  0.5f,   0.0f, 1.0f,
	 0.5f,  0.5f,   1.0f, 1.0f,
	 0.5f, -0.5f,   1.0f, 0.0f,
	-0.5f, -0.5f,   0.0f, 0.0f,
};

constexpr GLuint QuadIndices[] = { 0, 1, 2, 2, 3, 0 };

constexpr size_t QuadVertexStride = 4 * sizeof( float );

// 每個 instance(四邊形)各自的資料：
// 世界座標偏移 + 縮放 + texture layer + 要使用哪一組 texture array
struct InstanceData
{
	glm::vec3 offset;
	float     scale;
	int       layerIndex;
	int       materialIndex; // 0 = TextureManager A, 1 = TextureManager B
};

constexpr size_t InstanceStride = sizeof( InstanceData );

// 對應 glMultiDrawElementsIndirect 時所需的 command 結構
struct DrawElementsIndirectCommand
{
	GLuint count;
	GLuint instanceCount;
	GLuint firstIndex;
	GLint  baseVertex;
	GLuint baseInstance;
};

// SSBO 中儲存的 texture handle 陣列結構(需與 shader 中的 TextureBlock 對應)
struct TextureHandleBlock
{
	GLuint64 texArrays[2]; // 2 組 texture array 的 bindless handle
};

constexpr int WindowWidth  = 800;
constexpr int WindowHeight = 600;

constexpr int ImageCountA    = 4; // Texture Array A：128x128，4 張
constexpr int ImageCountB    = 3; // Texture Array B：256x256，3 張
constexpr int TotalImageCount = ImageCountA + ImageCountB;

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

	// Texture Array A：128x128，4 張圖片
	gl::TextureManager  textureManagerA;

	if ( !textureManagerA.build( { "brick", "grass", "sand", "water" }, 128, 128 ) )
	{
		fmt::print( "TextureManager A 建立失敗\n" );
		return EXIT_FAILURE;
	}

	// Texture Array B：256x256，3 張圖片(尺寸與圖片數量都與 A 不同)
	gl::TextureManager  textureManagerB;

	if ( !textureManagerB.build( { "lava", "stone", "wood" }, 256, 256 ) )
	{
		fmt::print( "TextureManager B 建立失敗\n" );
		return EXIT_FAILURE;
	}

	//--------------------------------------------------------------------------

	// 使用 StreamingBuffer 來管理 SSBO，儲存兩組 bindless texture handle
	// binding point = 1，每幀可更新(雖然這個例子中不需要)
	gl::StreamingBuffer textureHandleSSBO(
		GL_SHADER_STORAGE_BUFFER,
		sizeof( TextureHandleBlock ),
		3 );

	if ( !textureHandleSSBO.isValid() )
	{
		fmt::print( "Texture Handle SSBO 建立失敗(驅動可能不支援 ARB_buffer_storage)\n" );
		return EXIT_FAILURE;
	}

	//--------------------------------------------------------------------------

	auto VAO = std::make_shared<gl::VertexArray>();
	auto VBO = std::make_shared<gl::VertexBuffer>( sizeof( QuadVertices ), QuadVertices );
	auto EBO = std::make_shared<gl::ElementsBuffer>( sizeof( QuadIndices ), QuadIndices );

	auto bindingPointA = std::make_shared<gl::VertexBinding>( VAO );
	auto bindingPointB = std::make_shared<gl::VertexBinding>( VAO );

	VAO->bindEBO( EBO );
	bindingPointA->bindVBO( VBO->getID(), 0, static_cast<GLsizei>( QuadVertexStride ) );

	// 啟用 vertex 屬性 0 (位置) 與 1 (顏色)
	auto attrib_0 = std::make_shared<gl::VertexAttrib>( VAO, 0 );  // 位置(location = 0)
	auto attrib_1 = std::make_shared<gl::VertexAttrib>( VAO, 1 );  // 顏色(location = 1)
	auto attrib_2 = std::make_shared<gl::VertexAttrib>( VAO, 2 );  // xyz = 世界座標偏移, w = 縮放
	auto attrib_3 = std::make_shared<gl::VertexAttrib>( VAO, 3 );  // texture array 裡的第幾層
	auto attrib_4 = std::make_shared<gl::VertexAttrib>( VAO, 4 );  // 對應 SSBO 中第幾組 texture array

	// binding A：共用四邊形 vertex(位置 + UV)
	// 將屬性 0 和 1 都黏到綁定點
	attrib_0->setFormat( 2, GL_FLOAT, GL_FALSE, 0 );
	attrib_1->setFormat( 2, GL_FLOAT, GL_FALSE, 2 * sizeof( float ) );
	bindingPointA->attachAttrib( attrib_0 );
	bindingPointA->attachAttrib( attrib_1 );
//	bindingPointA->setDivisor( 0 );   // divisor = 0，表示每個 vertex 都要更新一次，預設已經是0了

	// binding B：instanced 屬性(每個 instance 各自的偏移/縮放/layer/材質索引)
	attrib_2->setFormat( 4, GL_FLOAT, GL_FALSE, offsetof( InstanceData, offset ) );
	attrib_3->setFormat( 1, GL_INT, offsetof( InstanceData, layerIndex ) );
	attrib_4->setFormat( 1, GL_INT, offsetof( InstanceData, materialIndex ) );
	bindingPointB->attachAttrib( attrib_2 );
	bindingPointB->attachAttrib( attrib_3 );
	bindingPointB->attachAttrib( attrib_4 );
	bindingPointB->setDivisor( 1 );   // divisor = 1，每個 instance 更新一次(一張圖片就是一個 instance)

	//--------------------------------------------------------------------------

	gl::StreamingBuffer cameraBuffer( GL_UNIFORM_BUFFER, sizeof( gl::Camera::UniformBlock ), 3 );
	gl::StreamingBuffer instanceBuffer( GL_ARRAY_BUFFER, sizeof( InstanceData ) * TotalImageCount, 3 );
	gl::StreamingBuffer indirectBuffer( GL_DRAW_INDIRECT_BUFFER, sizeof( DrawElementsIndirectCommand ) * TotalImageCount, 3 );

	if ( !cameraBuffer.isValid() || !instanceBuffer.isValid() || !indirectBuffer.isValid() )
	{
		fmt::print( "StreamingBuffer 建立失敗（驅動可能不支援 ARB_buffer_storage）\n" );
		return EXIT_FAILURE;
	}

	//--------------------------------------------------------------------------

	// 建立攝影機，往 +Z 退開才能看到排列在 Z = 0 平面上的圖片
	gl::Camera camera( glm::vec3( 0.0f, 0.0f, 4.0f ) );

	// 上排 4 張對應 Texture Array A，下排 3 張對應 Texture Array B
	constexpr glm::vec3 QuadOffsets[TotalImageCount] = {
		// Texture Array A（128x128, 4 張）
		{ -1.35f,  0.6f, 0.0f },
		{ -0.45f,  0.6f, 0.0f },
		{  0.45f,  0.6f, 0.0f },
		{  1.35f,  0.6f, 0.0f },
		// Texture Array B（256x256, 3 張）
		{ -0.9f, -0.6f, 0.0f },
		{  0.0f, -0.6f, 0.0f },
		{  0.9f, -0.6f, 0.0f },
	};

	constexpr float QuadScale = 0.8f;
	bool            quit     = false;
	SDL_Event       event;
	float           lastTick = sdl::GetTick();

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

		gl::ClearScreen();

		// 非同步上傳 Camera 矩陣至 UBO，並綁定到 binding = 0
		const float aspectRatio = static_cast<float>( WindowWidth ) / static_cast<float>( WindowHeight );
		camera.uploadToUBO( cameraBuffer, aspectRatio, 0 );

		// 非同步寫入 texture handle 至 SSBO，並綁定到 binding = 1
		void* textureHandleDst = textureHandleSSBO.beginWrite();

		if ( textureHandleDst != nullptr )
		{
			TextureHandleBlock handles;
			handles.texArrays[0] = textureManagerA.getHandle();
			handles.texArrays[1] = textureManagerB.getHandle();

			std::memcpy( textureHandleDst, &handles, sizeof( handles ) );

			textureHandleSSBO.bindRange( 1 );
			textureHandleSSBO.endWrite();

			// 非同步寫入所有 instance 的資料(位置 / 縮放 / layer index / 材質索引)
			void* instanceDst = instanceBuffer.beginWrite();

			if ( instanceDst != nullptr )
			{
				InstanceData instances[TotalImageCount];
				int          idx = 0;

				// Texture Array A：materialIndex = 0
				for ( int i = 0; i < ImageCountA; ++i, ++idx )
				{
					instances[idx].offset        = QuadOffsets[idx];
					instances[idx].scale         = QuadScale;
					instances[idx].layerIndex    = i;
					instances[idx].materialIndex = 0;
				}

				// Texture Array B：materialIndex = 1
				for ( int i = 0; i < ImageCountB; ++i, ++idx )
				{
					instances[idx].offset        = QuadOffsets[idx];
					instances[idx].scale         = QuadScale;
					instances[idx].layerIndex    = i;
					instances[idx].materialIndex = 1;
				}

				std::memcpy( instanceDst, instances, sizeof( instances ) );

				bindingPointB->bindVBO(
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
					DrawElementsIndirectCommand commands[TotalImageCount];

					for ( GLuint i = 0; i < TotalImageCount; ++i )
					{
						commands[i].count         = static_cast<GLuint>( std::size( QuadIndices ) );
						commands[i].instanceCount = 1;
						commands[i].firstIndex    = 0;
						commands[i].baseVertex    = 0;
						commands[i].baseInstance  = i;
					}

					std::memcpy( indirectDst, commands, sizeof( commands ) );

					myShader.use();
					VAO->bind();

					glBindBuffer( GL_DRAW_INDIRECT_BUFFER, indirectBuffer.getBufferId() );

					// 一次呼叫，透過 indirect buffer 描述的多筆 command
					// 分別繪製各張圖片(各自對應不同的 texture array 及 layer)
					glMultiDrawElementsIndirect(
						GL_TRIANGLES,
						GL_UNSIGNED_INT,
						reinterpret_cast<const void*>( indirectBuffer.getCurrentOffset() ),
						TotalImageCount,
						sizeof( DrawElementsIndirectCommand ) );

					indirectBuffer.endWrite();
				}
			}
		}

		app.refresh();
	}

	//--------------------------------------------------------------------------

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
