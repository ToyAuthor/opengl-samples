/*
 * 範例名稱：sample06
 * -----------------------------------------------------------------------------
 * 使用 FPS 遊戲方式操作視角
 * 採用 UBO 來傳送 Camera 的 View / Projection 矩陣
 *
 * 不使用延遲著色，採用"向前渲染"
 * 三個光源
 * SSAO
 * Normal Mapping
 *
 * 向前渲染要做 SSAO 的難處在於：SSAO 需要整個畫面的深度與法線，
 * 但向前渲染在著色當下並沒有這些資料。
 * 所以本範例的管線是：
 *   1. Depth / Normal Pre-Pass：只輸出 view space 的 position 與 normal
 *                               (比延遲著色的 G-Buffer 輕，不含 albedo / 材質)
 *                               順便把深度緩衝填好
 *   2. SSAO Pass  ：半球取樣核心 + 隨機旋轉噪點，產生 AO 貼圖
 *   3. Blur Pass  ：4x4 box blur 消除噪點條紋
 *   4. Forward Pass：真正的著色，三個光源在同一個 fragment shader 裡一次算完
 *                    法線取自軟體產生的 Normal Map，經 TBN 轉到 view space
 *                    AO 則用 gl_FragCoord 換算螢幕 UV 去查第 3 步的結果
 *                    深度測試改用 GL_EQUAL，直接沿用 Pre-Pass 的深度，不會重複著色
 *
 * 所有貼圖都由程式自行產生，不需準備任何圖檔
 */

#include <array>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <fmt/core.h>
#include <glad/glad.h>
#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "sdl/Utils.hpp"
#include "sdl/Window.hpp"
#include "gl/Core.hpp"
#include "gl/ShaderProgram.hpp"
#include "gl/Camera.hpp"           // 提供一個簡單的 FPS 攝影機，並提供 std140 UBO 的 UniformBlock 結構
#include "gl/StreamingBuffer.hpp"  // 用來建立一個 CPU 與 GPU 之間的 Persistent Mapping buffer，讓 CPU 可以非同步地寫入資料到 GPU
#include "gl/ImageData.hpp"
#include "gl/CreateImage.hpp"

namespace{

constexpr int WindowWidth  = 1280;
constexpr int WindowHeight = 720;

constexpr int SSAOKernelSize = 32;   // 半球取樣點數量
constexpr int SSAONoiseSide  = 4;    // 隨機旋轉噪點貼圖邊長(4x4)
constexpr int LightCount     = 3;    // 向前渲染：三個光源在同一次著色中處理完

//------------------------------------------------------------------------------
// Shader 原始碼
//------------------------------------------------------------------------------

// Pre-Pass：只寫出 SSAO 需要的 view space position / normal，順便建立深度
const char* PrePassVS = R"(
	#version 460 core

	layout (location = 0) in vec3 aPos;
	layout (location = 1) in vec3 aNormal;
	layout (location = 2) in vec3 aTangent;
	layout (location = 3) in vec2 aUV;

	layout (std140, binding = 0) uniform CameraBlock
	{
		mat4 view;
		mat4 projection;
		mat4 viewProjection;
	} camera;

	layout (location = 0) uniform mat4 model;
	layout (location = 1) uniform mat3 normalMatrix;  // view space 的法線矩陣
	layout (location = 2) uniform vec2 uvScale;

	layout (location = 0) out vec3 vViewPos;
	layout (location = 1) out vec2 vUV;
	layout (location = 2) out mat3 vTBN;  // 佔用 location 2、3、4

	void main()
	{
		vec4 viewPos = camera.view * model * vec4( aPos, 1.0 );

		vViewPos = viewPos.xyz;
		vUV      = aUV * uvScale;

		// 把 TBN 三軸一起帶到 view space，Normal Map 取出的切線空間法線才能正確轉換
		vec3 N = normalize( normalMatrix * aNormal );
		vec3 T = normalize( normalMatrix * aTangent );

		// Gram-Schmidt 正交化，避免插值與非等向縮放造成 T 不垂直 N
		T = normalize( T - dot( T, N ) * N );

		vec3 B = cross( N, T );

		vTBN = mat3( T, B, N );

		gl_Position = camera.projection * viewPos;
	}
)";

const char* PrePassFS = R"(
	#version 460 core

	layout (location = 0) in vec3 vViewPos;
	layout (location = 1) in vec2 vUV;
	layout (location = 2) in mat3 vTBN;

	layout (binding = 1) uniform sampler2D normalMap;

	layout (location = 0) out vec4 outPosition;  // view space 位置
	layout (location = 1) out vec4 outNormal;    // view space 法線

	void main()
	{
		// SSAO 若用幾何法線，Normal Mapping 的細節就不會參與遮蔽計算，
		// 所以這裡一樣採樣 Normal Map，讓 AO 跟著凹凸走
		vec3 tangentNormal = texture( normalMap, vUV ).rgb * 2.0 - 1.0;

		outPosition = vec4( vViewPos, 1.0 );
		outNormal   = vec4( normalize( vTBN * tangentNormal ), 0.0 );
	}
)";

// 全螢幕三角形：不需要 VBO，直接用 gl_VertexID 算出座標
const char* FullscreenVS = R"(
	#version 460 core

	layout (location = 0) out vec2 vUV;

	void main()
	{
		vUV = vec2( ( gl_VertexID << 1 ) & 2, gl_VertexID & 2 );
		gl_Position = vec4( vUV * 2.0 - 1.0, 0.0, 1.0 );
	}
)";

// SSAO Pass：在 view space 做半球取樣
const char* SSAOFS = R"(
	#version 460 core

	layout (location = 0) in  vec2 vUV;
	layout (location = 0) out float FragAO;

	layout (std140, binding = 0) uniform CameraBlock
	{
		mat4 view;
		mat4 projection;
		mat4 viewProjection;
	} camera;

	layout (binding = 0) uniform sampler2D gPosition;
	layout (binding = 1) uniform sampler2D gNormal;
	layout (binding = 2) uniform sampler2D texNoise;

	const int KernelSize = 32;

	uniform vec3  samples[KernelSize];
	uniform vec2  noiseScale;
	uniform float radius;
	uniform float bias;

	void main()
	{
		vec3 fragPos = texture( gPosition, vUV ).xyz;
		vec3 normal  = normalize( texture( gNormal, vUV ).xyz );

		// 每 4x4 像素套用一組隨機旋轉，等效於把取樣核心旋轉，用少量取樣換取雜訊
		vec3 randomVec = texture( texNoise, vUV * noiseScale ).xyz;

		vec3 tangent   = normalize( randomVec - normal * dot( randomVec, normal ) );
		vec3 bitangent = cross( normal, tangent );
		mat3 TBN       = mat3( tangent, bitangent, normal );

		float occlusion = 0.0;

		for ( int i = 0; i < KernelSize; ++i )
		{
			// 取樣點從切線空間半球轉到 view space
			vec3 samplePos = fragPos + ( TBN * samples[i] ) * radius;

			// 投影回螢幕座標，才能查詢該位置實際被寫入的深度
			vec4 offset = camera.projection * vec4( samplePos, 1.0 );
			offset.xyz /= offset.w;
			offset.xyz  = offset.xyz * 0.5 + 0.5;

			float sampleDepth = texture( gPosition, offset.xy ).z;

			// 距離太遠的幾何不該互相遮蔽，用 smoothstep 做範圍檢查
			float rangeCheck = smoothstep( 0.0, 1.0, radius / abs( fragPos.z - sampleDepth ) );

			occlusion += ( sampleDepth >= samplePos.z + bias ? 1.0 : 0.0 ) * rangeCheck;
		}

		FragAO = 1.0 - ( occlusion / float( KernelSize ) );
	}
)";

// Blur Pass：把 SSAO 的噪點抹平
const char* BlurFS = R"(
	#version 460 core

	layout (location = 0) in  vec2 vUV;
	layout (location = 0) out float FragAO;

	layout (binding = 0) uniform sampler2D ssaoInput;

	void main()
	{
		vec2  texelSize = 1.0 / vec2( textureSize( ssaoInput, 0 ) );
		float result    = 0.0;

		for ( int x = -2; x < 2; ++x )
		{
			for ( int y = -2; y < 2; ++y )
			{
				result += texture( ssaoInput, vUV + vec2( x, y ) * texelSize ).r;
			}
		}

		FragAO = result / 16.0;
	}
)";

// Forward Pass：直接在幾何上完成 Blinn-Phong 著色，三個光源一次算完
// 頂點階段和 Pre-Pass 完全相同，所以共用 PrePassVS
const char* ForwardFS = R"(
	#version 460 core

	layout (location = 0) in vec3 vViewPos;
	layout (location = 1) in vec2 vUV;
	layout (location = 2) in mat3 vTBN;

	layout (binding = 0) uniform sampler2D albedoMap;
	layout (binding = 1) uniform sampler2D normalMap;
	layout (binding = 2) uniform sampler2D ssaoMap;   // 螢幕空間的 AO

	layout (location = 0) out vec4 FragColor;

	const int LightCount = 3;

	// 向前渲染的特徵：光源參數直接餵給著色幾何的 shader
	struct Light
	{
		vec3  viewPos;    // 光源在 view space 的位置
		vec3  color;
		float linearTerm;
		float quadratic;
	};

	uniform Light lights[LightCount];

	void main()
	{
		// Normal Map 存的是 [0,1]，要還原成 [-1,1] 的切線空間向量
		vec3 tangentNormal = texture( normalMap, vUV ).rgb * 2.0 - 1.0;
		vec3 normal        = normalize( vTBN * tangentNormal );

		vec3 albedo  = texture( albedoMap, vUV ).rgb;
		vec3 viewDir = normalize( -vViewPos );   // view space 下攝影機在原點

		// 向前渲染沒有 G-Buffer 的螢幕座標可用，改由 gl_FragCoord 推算 AO 貼圖的 UV
		vec2  screenUV = gl_FragCoord.xy / vec2( textureSize( ssaoMap, 0 ) );
		float ao       = texture( ssaoMap, screenUV ).r;

		vec3 color = 0.25 * albedo * ao;   // 環境光乘上 AO

		for ( int i = 0; i < LightCount; ++i )
		{
			vec3  lightDir = normalize( lights[i].viewPos - vViewPos );
			vec3  halfway  = normalize( lightDir + viewDir );

			float diff = max( dot( normal, lightDir ), 0.0 );
			float spec = pow( max( dot( normal, halfway ), 0.0 ), 64.0 );

			float dist  = length( lights[i].viewPos - vViewPos );
			float atten = 1.0 / ( 1.0 + lights[i].linearTerm * dist + lights[i].quadratic * dist * dist );

			color += albedo * diff * lights[i].color * atten;
			color += vec3( 0.35 ) * spec * lights[i].color * atten;
		}

		// 簡單的 gamma 校正，讓暗部的 AO 變化看得更清楚
		FragColor = vec4( pow( color, vec3( 1.0 / 2.2 ) ), 1.0 );
	}
)";

//------------------------------------------------------------------------------
// 幾何資料
//------------------------------------------------------------------------------

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 tangent;
	glm::vec2 uv;
};

// 依照一組正交基底產生一個面(兩個三角形)，讓立方體與地板共用同一份程式碼
void AppendFace(
	std::vector<Vertex>& out,
	const glm::vec3&     normal,
	const glm::vec3&     tangent,
	float                halfSize )
{
	const glm::vec3 bitangent = glm::cross( normal, tangent );
	const glm::vec3 center    = normal * halfSize;

	const glm::vec3 p0 = center - tangent * halfSize - bitangent * halfSize;
	const glm::vec3 p1 = center + tangent * halfSize - bitangent * halfSize;
	const glm::vec3 p2 = center + tangent * halfSize + bitangent * halfSize;
	const glm::vec3 p3 = center - tangent * halfSize + bitangent * halfSize;

	out.push_back( { p0, normal, tangent, { 0.0f, 0.0f } } );
	out.push_back( { p1, normal, tangent, { 1.0f, 0.0f } } );
	out.push_back( { p2, normal, tangent, { 1.0f, 1.0f } } );

	out.push_back( { p0, normal, tangent, { 0.0f, 0.0f } } );
	out.push_back( { p2, normal, tangent, { 1.0f, 1.0f } } );
	out.push_back( { p3, normal, tangent, { 0.0f, 1.0f } } );
}

std::vector<Vertex> CreateCubeVertices()
{
	std::vector<Vertex> vertices;
	vertices.reserve( 36 );

	AppendFace( vertices, {  0.0f,  0.0f,  1.0f }, {  1.0f,  0.0f,  0.0f }, 0.5f );
	AppendFace( vertices, {  0.0f,  0.0f, -1.0f }, { -1.0f,  0.0f,  0.0f }, 0.5f );
	AppendFace( vertices, {  1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f, -1.0f }, 0.5f );
	AppendFace( vertices, { -1.0f,  0.0f,  0.0f }, {  0.0f,  0.0f,  1.0f }, 0.5f );
	AppendFace( vertices, {  0.0f,  1.0f,  0.0f }, {  1.0f,  0.0f,  0.0f }, 0.5f );
	AppendFace( vertices, {  0.0f, -1.0f,  0.0f }, {  1.0f,  0.0f,  0.0f }, 0.5f );

	return vertices;
}

//------------------------------------------------------------------------------
// 軟體產生貼圖
//------------------------------------------------------------------------------

// 產生高度場：磚塊接縫 + 圓形凸起，之後用來推導 Normal Map
float BumpHeight( int x, int y, int width, int height )
{
	const float u = static_cast<float>( x ) / static_cast<float>( width );
	const float v = static_cast<float>( y ) / static_cast<float>( height );

	// 磚塊接縫：每隔一列水平位移半塊
	const float row    = std::floor( v * 4.0f );
	const float shift  = ( static_cast<int>( row ) % 2 == 0 ) ? 0.0f : 0.5f;
	const float brickU = std::fmod( u * 4.0f + shift, 1.0f );
	const float brickV = std::fmod( v * 4.0f, 1.0f );
	const float edge   = 0.06f;

	float h = 1.0f;

	if ( brickU < edge || brickU > 1.0f - edge ) h = 0.0f;
	if ( brickV < edge || brickV > 1.0f - edge ) h = 0.0f;

	// 每塊磚中央加一顆圓形凸起，讓打光的立體感更明顯
	const float dx = brickU - 0.5f;
	const float dy = brickV - 0.5f;
	const float d  = std::sqrt( dx * dx + dy * dy );

	if ( d < 0.25f )
	{
		h += 0.6f * std::cos( d / 0.25f * 1.5707963f );
	}

	return h;
}

// 以中央差分(Sobel 的簡化版)把高度場轉成切線空間 Normal Map
void CreateNormalMapImage( ::gl::ImageData& img, float strength )
{
	img.pixels.resize( static_cast<size_t>( img.width ) * img.height * img.channels );

	auto sampleHeight = [&]( int x, int y )
	{
		// 用環繞取樣，貼圖才能無縫重複
		const int sx = ( x + img.width  ) % img.width;
		const int sy = ( y + img.height ) % img.height;

		return BumpHeight( sx, sy, img.width, img.height );
	};

	for ( int y = 0; y < img.height; ++y )
	{
		for ( int x = 0; x < img.width; ++x )
		{
			const float dhdx = sampleHeight( x + 1, y ) - sampleHeight( x - 1, y );
			const float dhdy = sampleHeight( x, y + 1 ) - sampleHeight( x, y - 1 );

			const glm::vec3 n = glm::normalize( glm::vec3( -dhdx * strength, -dhdy * strength, 1.0f ) );

			const size_t i = ( static_cast<size_t>( y ) * img.width + x ) * img.channels;

			img.pixels[i + 0] = static_cast<unsigned char>( ( n.x * 0.5f + 0.5f ) * 255.0f );
			img.pixels[i + 1] = static_cast<unsigned char>( ( n.y * 0.5f + 0.5f ) * 255.0f );
			img.pixels[i + 2] = static_cast<unsigned char>( ( n.z * 0.5f + 0.5f ) * 255.0f );
			img.pixels[i + 3] = 255;
		}
	}
}

GLuint CreateTexture2D( const ::gl::ImageData& img, GLenum internalFormat )
{
	GLuint texture = 0;

	glCreateTextures( GL_TEXTURE_2D, 1, &texture );
	glTextureStorage2D( texture, 1, internalFormat, img.width, img.height );
	glTextureSubImage2D( texture, 0, 0, 0, img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data() );

	glTextureParameteri( texture, GL_TEXTURE_WRAP_S,     GL_REPEAT );
	glTextureParameteri( texture, GL_TEXTURE_WRAP_T,     GL_REPEAT );
	glTextureParameteri( texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTextureParameteri( texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

	return texture;
}

// Pre-Pass / SSAO 等中繼貼圖：必須是 NEAREST + CLAMP，取樣才不會跨越邊界污染資料
GLuint CreateRenderTarget( GLenum internalFormat, int width, int height )
{
	GLuint texture = 0;

	glCreateTextures( GL_TEXTURE_2D, 1, &texture );
	glTextureStorage2D( texture, 1, internalFormat, width, height );

	glTextureParameteri( texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTextureParameteri( texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTextureParameteri( texture, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE );
	glTextureParameteri( texture, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE );

	return texture;
}

//------------------------------------------------------------------------------
// 輸入處理
//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------

int main3( sdl::Window& app, std::shared_ptr<gl::Core> core )
{
	gl::ShaderProgram prePassShader( PrePassVS,    PrePassFS );
	gl::ShaderProgram ssaoShader   ( FullscreenVS, SSAOFS );
	gl::ShaderProgram blurShader   ( FullscreenVS, BlurFS );
	gl::ShaderProgram forwardShader( PrePassVS,    ForwardFS );  // 頂點階段與 Pre-Pass 共用

	//--------------------------------------------------------------------------
	// 幾何：一個立方體的頂點資料，靠不同的 model 矩陣重複使用
	//--------------------------------------------------------------------------

	const std::vector<Vertex> cubeVertices = CreateCubeVertices();

	GLuint cubeVBO = 0;
	glCreateBuffers( 1, &cubeVBO );
	glNamedBufferStorage(
		cubeVBO,
		static_cast<GLsizeiptr>( cubeVertices.size() * sizeof( Vertex ) ),
		cubeVertices.data(),
		0 );

	GLuint VAO = 0;
	glCreateVertexArrays( 1, &VAO );

	glVertexArrayVertexBuffer( VAO, 0, cubeVBO, 0, static_cast<GLsizei>( sizeof( Vertex ) ) );

	// 四個屬性都掛在綁定點 0：位置 / 法線 / 切線 / UV
	constexpr GLuint AttribCount = 4;
	const GLint  sizes  [AttribCount] = { 3, 3, 3, 2 };
	const GLuint offsets[AttribCount] = {
		offsetof( Vertex, position ),
		offsetof( Vertex, normal ),
		offsetof( Vertex, tangent ),
		offsetof( Vertex, uv ) };

	for ( GLuint i = 0; i < AttribCount; ++i )
	{
		glEnableVertexArrayAttrib ( VAO, i );
		glVertexArrayAttribFormat ( VAO, i, sizes[i], GL_FLOAT, GL_FALSE, offsets[i] );
		glVertexArrayAttribBinding( VAO, i, 0 );
	}

	// SSAO / Blur 等全螢幕 pass 不需要頂點資料，但 OpenGL 仍要求綁定一個 VAO
	GLuint emptyVAO = 0;
	glCreateVertexArrays( 1, &emptyVAO );

	//--------------------------------------------------------------------------
	// 貼圖：Albedo 由 CreateImage 產生，Normal Map 由高度場推導
	//--------------------------------------------------------------------------

	::gl::ImageData albedoImage;
	albedoImage.width  = 256;
	albedoImage.height = 256;
	::gl::CreateImage( "sample06-albedo", albedoImage );

	::gl::ImageData normalImage;
	normalImage.width  = 256;
	normalImage.height = 256;
	CreateNormalMapImage( normalImage, 6.0f );

	const GLuint albedoTexture = CreateTexture2D( albedoImage, GL_SRGB8_ALPHA8 );
	const GLuint normalTexture = CreateTexture2D( normalImage, GL_RGBA8 );

	//--------------------------------------------------------------------------
	// Pre-Pass 的輸出：只有 position / normal 兩張，比延遲著色少一張 albedo
	// 深度用貼圖(而非 renderbuffer)並沒有必要，但改用 renderbuffer 較省
	//--------------------------------------------------------------------------

	const GLuint prePassPosition = CreateRenderTarget( GL_RGBA16F, WindowWidth, WindowHeight );
	const GLuint prePassNormal   = CreateRenderTarget( GL_RGBA16F, WindowWidth, WindowHeight );

	GLuint depthBuffer = 0;
	glCreateRenderbuffers( 1, &depthBuffer );
	glNamedRenderbufferStorage( depthBuffer, GL_DEPTH_COMPONENT24, WindowWidth, WindowHeight );

	GLuint prePassFBO = 0;
	glCreateFramebuffers( 1, &prePassFBO );
	glNamedFramebufferTexture( prePassFBO, GL_COLOR_ATTACHMENT0, prePassPosition, 0 );
	glNamedFramebufferTexture( prePassFBO, GL_COLOR_ATTACHMENT1, prePassNormal,   0 );
	glNamedFramebufferRenderbuffer( prePassFBO, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer );

	const GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glNamedFramebufferDrawBuffers( prePassFBO, 2, drawBuffers );

	/*
	 * 向前渲染的著色結果要寫進「自己的」framebuffer，而不是直接畫到視窗，
	 * 原因是必須沿用 Pre-Pass 已經算好的深度緩衝(GL_EQUAL 深度測試)，
	 * 而預設 framebuffer 的深度緩衝沒辦法跟 FBO 共用。
	 * 最後再把顏色 blit 回視窗。
	 */
	const GLuint sceneColor = CreateRenderTarget( GL_RGBA8, WindowWidth, WindowHeight );

	GLuint forwardFBO = 0;
	glCreateFramebuffers( 1, &forwardFBO );
	glNamedFramebufferTexture( forwardFBO, GL_COLOR_ATTACHMENT0, sceneColor, 0 );
	glNamedFramebufferRenderbuffer( forwardFBO, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer );

	// SSAO 與模糊結果各一張單通道貼圖
	const GLuint ssaoTexture     = CreateRenderTarget( GL_R8, WindowWidth, WindowHeight );
	const GLuint ssaoBlurTexture = CreateRenderTarget( GL_R8, WindowWidth, WindowHeight );

	GLuint ssaoFBO = 0;
	glCreateFramebuffers( 1, &ssaoFBO );
	glNamedFramebufferTexture( ssaoFBO, GL_COLOR_ATTACHMENT0, ssaoTexture, 0 );

	GLuint ssaoBlurFBO = 0;
	glCreateFramebuffers( 1, &ssaoBlurFBO );
	glNamedFramebufferTexture( ssaoBlurFBO, GL_COLOR_ATTACHMENT0, ssaoBlurTexture, 0 );

	if ( glCheckNamedFramebufferStatus( prePassFBO,  GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ||
	     glCheckNamedFramebufferStatus( forwardFBO,  GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ||
	     glCheckNamedFramebufferStatus( ssaoFBO,     GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ||
	     glCheckNamedFramebufferStatus( ssaoBlurFBO, GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
	{
		fmt::print( "Framebuffer 建立失敗\n" );
		return EXIT_FAILURE;
	}

	//--------------------------------------------------------------------------
	// SSAO 取樣核心與噪點貼图
	//--------------------------------------------------------------------------

	std::uniform_real_distribution<float> random01( 0.0f, 1.0f );
	std::default_random_engine            generator( 12345u );

	std::vector<glm::vec3> ssaoKernel;
	ssaoKernel.reserve( SSAOKernelSize );

	for ( int i = 0; i < SSAOKernelSize; ++i )
	{
		glm::vec3 sample(
			random01( generator ) * 2.0f - 1.0f,
			random01( generator ) * 2.0f - 1.0f,
			random01( generator ) );          // z 只取正值，取樣點落在法線半球內

		sample = glm::normalize( sample ) * random01( generator );

		// 用二次曲線讓取樣點往中心集中，近處遮蔽的權重較高
		const float scale = static_cast<float>( i ) / static_cast<float>( SSAOKernelSize );

		ssaoKernel.push_back( sample * glm::mix( 0.1f, 1.0f, scale * scale ) );
	}

	std::vector<glm::vec3> noiseData;
	noiseData.reserve( SSAONoiseSide * SSAONoiseSide );

	for ( int i = 0; i < SSAONoiseSide * SSAONoiseSide; ++i )
	{
		// z = 0：旋轉軸固定在切線平面上，只做繞法線的隨機旋轉
		noiseData.emplace_back(
			random01( generator ) * 2.0f - 1.0f,
			random01( generator ) * 2.0f - 1.0f,
			0.0f );
	}

	GLuint noiseTexture = 0;
	glCreateTextures( GL_TEXTURE_2D, 1, &noiseTexture );
	glTextureStorage2D( noiseTexture, 1, GL_RGBA16F, SSAONoiseSide, SSAONoiseSide );
	glTextureSubImage2D( noiseTexture, 0, 0, 0, SSAONoiseSide, SSAONoiseSide, GL_RGB, GL_FLOAT, noiseData.data() );
	glTextureParameteri( noiseTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTextureParameteri( noiseTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTextureParameteri( noiseTexture, GL_TEXTURE_WRAP_S,     GL_REPEAT );
	glTextureParameteri( noiseTexture, GL_TEXTURE_WRAP_T,     GL_REPEAT );

	// 取得非 layout 指定的 uniform 位置
	const GLint locSamples    = glGetUniformLocation( ssaoShader.getID(), "samples" );
	const GLint locNoiseScale = glGetUniformLocation( ssaoShader.getID(), "noiseScale" );
	const GLint locRadius     = glGetUniformLocation( ssaoShader.getID(), "radius" );
	const GLint locBias       = glGetUniformLocation( ssaoShader.getID(), "bias" );

	// 取樣核心是常數，只需要上傳一次
	glProgramUniform3fv( ssaoShader.getID(), locSamples, SSAOKernelSize, glm::value_ptr( ssaoKernel[0] ) );
	glProgramUniform2f ( ssaoShader.getID(), locNoiseScale,
		static_cast<float>( WindowWidth )  / static_cast<float>( SSAONoiseSide ),
		static_cast<float>( WindowHeight ) / static_cast<float>( SSAONoiseSide ) );
	glProgramUniform1f ( ssaoShader.getID(), locRadius, 0.6f );
	glProgramUniform1f ( ssaoShader.getID(), locBias,   0.025f );

	//--------------------------------------------------------------------------
	// 三個光源：陣列成員的 uniform 位置要逐一查詢
	//--------------------------------------------------------------------------

	struct LightUniform
	{
		GLint viewPos;
		GLint color;
		GLint linearTerm;
		GLint quadratic;
	};

	std::array<LightUniform, LightCount> lightUniforms{};

	for ( int i = 0; i < LightCount; ++i )
	{
		const std::string prefix = fmt::format( "lights[{}].", i );

		lightUniforms[i].viewPos    = glGetUniformLocation( forwardShader.getID(), ( prefix + "viewPos" ).c_str() );
		lightUniforms[i].color      = glGetUniformLocation( forwardShader.getID(), ( prefix + "color" ).c_str() );
		lightUniforms[i].linearTerm = glGetUniformLocation( forwardShader.getID(), ( prefix + "linearTerm" ).c_str() );
		lightUniforms[i].quadratic  = glGetUniformLocation( forwardShader.getID(), ( prefix + "quadratic" ).c_str() );
	}

	// 三個顏色不同的光源，繞場景的相位也錯開，方便觀察 Normal Mapping 的凹凸感
	const std::array<glm::vec3, LightCount> lightColors = {{
		{ 1.0f, 0.92f, 0.80f },   // 暖白
		{ 0.25f, 0.55f, 1.0f },   // 藍
		{ 1.0f, 0.35f, 0.30f },   // 紅
	}};

	for ( int i = 0; i < LightCount; ++i )
	{
		glProgramUniform3fv( forwardShader.getID(), lightUniforms[i].color, 1, glm::value_ptr( lightColors[i] ) );
		glProgramUniform1f ( forwardShader.getID(), lightUniforms[i].linearTerm, 0.09f );
		glProgramUniform1f ( forwardShader.getID(), lightUniforms[i].quadratic,  0.032f );
	}

	//--------------------------------------------------------------------------

	// Camera 的 View / Projection 矩陣用 StreamingBuffer 建立 UBO，
	// 每幀非同步上傳，不需等待 GPU 完成上一幀的 draw call 才能寫入
	gl::StreamingBuffer cameraBuffer( GL_UNIFORM_BUFFER, sizeof( gl::Camera::UniformBlock ), 3 );

	if ( !cameraBuffer.isValid() )
	{
		fmt::print( "StreamingBuffer 建立失敗(驅動可能不支援 ARB_buffer_storage)\n" );
		return EXIT_FAILURE;
	}

	// 建立攝影機，初始位置退開一段距離才看得到整個場景
	gl::Camera camera( glm::vec3( 0.0f, 1.2f, 5.0f ) );

	// 場景擺設：一個當地板的扁平大方塊，加上幾個大小不一的立方體
	struct Instance
	{
		glm::vec3 position;
		glm::vec3 scale;
		float     rotationDeg;
		glm::vec2 uvScale;
	};

	const std::array<Instance, 6> instances = {{
		{ {  0.0f, -1.0f,  0.0f }, { 12.0f, 0.4f, 12.0f },   0.0f, { 6.0f, 6.0f } },  // 地板
		{ { -1.6f, -0.3f, -0.4f }, {  1.2f, 1.2f,  1.2f },  25.0f, { 1.0f, 1.0f } },
		{ {  1.5f, -0.4f,  0.6f }, {  1.0f, 1.0f,  1.0f }, -15.0f, { 1.0f, 1.0f } },
		{ {  0.0f, -0.55f,-2.0f }, {  0.8f, 0.8f,  0.8f },  45.0f, { 1.0f, 1.0f } },
		{ {  2.6f,  0.2f, -1.6f }, {  1.6f, 2.4f,  1.6f },  10.0f, { 2.0f, 2.0f } },
		{ { -2.8f,  0.1f,  1.8f }, {  0.6f, 2.2f,  0.6f }, -35.0f, { 1.0f, 2.0f } },
	}};

	// 繪製整個場景的共用程式：Pre-Pass 與 Forward Pass 都會用到
	auto drawScene = [&]( gl::ShaderProgram& shader, const glm::mat4& view )
	{
		shader.use();
		glBindVertexArray( VAO );

		for ( const Instance& instance : instances )
		{
			glm::mat4 model = glm::translate( glm::mat4( 1.0f ), instance.position );
			model = glm::rotate( model, glm::radians( instance.rotationDeg ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
			model = glm::scale( model, instance.scale );

			// 法線矩陣要含 view，法線才和 position 同在 view space
			const glm::mat3 normalMatrix = glm::transpose( glm::inverse( glm::mat3( view * model ) ) );

			glProgramUniformMatrix4fv( shader.getID(), 0, 1, GL_FALSE, glm::value_ptr( model ) );
			glProgramUniformMatrix3fv( shader.getID(), 1, 1, GL_FALSE, glm::value_ptr( normalMatrix ) );
			glProgramUniform2f       ( shader.getID(), 2, instance.uvScale.x, instance.uvScale.y );

			glDrawArrays( GL_TRIANGLES, 0, static_cast<GLsizei>( cubeVertices.size() ) );
		}
	};

	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );

	bool      quit = false;
	SDL_Event event;
	float     lastTick = sdl::GetTick();

	const float aspectRatio = static_cast<float>( WindowWidth ) / static_cast<float>( WindowHeight );

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
					static_cast<float>( event.motion.xrel ),
					static_cast<float>( -event.motion.yrel ) );
				break;
			default:
				break;
			}
		}

		HandleKeyboardInput( camera, deltaTime );

		// 非同步上傳 Camera 矩陣至 UBO，並綁定到 binding = 0
		camera.uploadToUBO( cameraBuffer, aspectRatio, 0 );

		const glm::mat4 view = camera.getViewMatrix();

		//----------------------------------------------------------------------
		// 1. Depth / Normal Pre-Pass
		//----------------------------------------------------------------------

		glBindFramebuffer( GL_FRAMEBUFFER, prePassFBO );
		glViewport( 0, 0, WindowWidth, WindowHeight );

		const GLfloat clearZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		const GLfloat clearDepth   = 1.0f;

		glClearNamedFramebufferfv( prePassFBO, GL_COLOR, 0, clearZero );
		glClearNamedFramebufferfv( prePassFBO, GL_COLOR, 1, clearZero );
		glClearNamedFramebufferfv( prePassFBO, GL_DEPTH, 0, &clearDepth );

		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LESS );
		glDepthMask( GL_TRUE );

		glBindTextureUnit( 1, normalTexture );

		drawScene( prePassShader, view );

		//----------------------------------------------------------------------
		// 2. SSAO Pass（全螢幕 pass 不需要深度測試）
		//----------------------------------------------------------------------

		glDisable( GL_DEPTH_TEST );
		glBindVertexArray( emptyVAO );

		glBindFramebuffer( GL_FRAMEBUFFER, ssaoFBO );

		glBindTextureUnit( 0, prePassPosition );
		glBindTextureUnit( 1, prePassNormal );
		glBindTextureUnit( 2, noiseTexture );

		ssaoShader.use();
		glDrawArrays( GL_TRIANGLES, 0, 3 );

		//----------------------------------------------------------------------
		// 3. Blur Pass
		//----------------------------------------------------------------------

		glBindFramebuffer( GL_FRAMEBUFFER, ssaoBlurFBO );
		glBindTextureUnit( 0, ssaoTexture );

		blurShader.use();
		glDrawArrays( GL_TRIANGLES, 0, 3 );

		//----------------------------------------------------------------------
		// 4. Forward Pass：直接在幾何上完成三個光源的著色
		//----------------------------------------------------------------------

		glBindFramebuffer( GL_FRAMEBUFFER, forwardFBO );
		glClearNamedFramebufferfv( forwardFBO, GL_COLOR, 0, clearZero );

		/*
		 * 深度已經由 Pre-Pass 填好，這裡用 GL_EQUAL 只著色最前面的那個 fragment，
		 * 並關閉深度寫入，等於免費得到 early-Z，
		 * 這是向前渲染對付 overdraw 的標準手法
		 */
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_EQUAL );
		glDepthMask( GL_FALSE );

		// 三個光源各自繞場景移動，相位差 120 度
		for ( int i = 0; i < LightCount; ++i )
		{
			const float angle = currentTick * 0.6f + glm::radians( 120.0f * static_cast<float>( i ) );

			const glm::vec3 lightWorldPos(
				std::cos( angle ) * 3.2f,
				1.6f + std::sin( angle * 1.3f ) * 0.8f,
				std::sin( angle ) * 3.2f );

			const glm::vec3 lightViewPos = glm::vec3( view * glm::vec4( lightWorldPos, 1.0f ) );

			glProgramUniform3fv( forwardShader.getID(), lightUniforms[i].viewPos, 1, glm::value_ptr( lightViewPos ) );
		}

		glBindTextureUnit( 0, albedoTexture );
		glBindTextureUnit( 1, normalTexture );
		glBindTextureUnit( 2, ssaoBlurTexture );

		drawScene( forwardShader, view );

		// 還原深度狀態，避免影響下一幀的 Pre-Pass
		glDepthFunc( GL_LESS );
		glDepthMask( GL_TRUE );

		//----------------------------------------------------------------------
		// 5. 把著色結果貼回視窗
		//----------------------------------------------------------------------

		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		core->clear();

		glBlitNamedFramebuffer(
			forwardFBO, 0,
			0, 0, WindowWidth, WindowHeight,
			0, 0, WindowWidth, WindowHeight,
			GL_COLOR_BUFFER_BIT, GL_NEAREST );

		app.refresh();
	}

	//--------------------------------------------------------------------------
	// 釋放資源
	//--------------------------------------------------------------------------

	const GLuint textures[] = { albedoTexture, normalTexture, prePassPosition, prePassNormal,
	                            sceneColor, ssaoTexture, ssaoBlurTexture, noiseTexture };
	const GLuint fbos[]     = { prePassFBO, forwardFBO, ssaoFBO, ssaoBlurFBO };
	const GLuint vaos[]     = { VAO, emptyVAO };

	glDeleteTextures( static_cast<GLsizei>( std::size( textures ) ), textures );
	glDeleteFramebuffers( static_cast<GLsizei>( std::size( fbos ) ), fbos );
	glDeleteRenderbuffers( 1, &depthBuffer );
	glDeleteVertexArrays( static_cast<GLsizei>( std::size( vaos ) ), vaos );
	glDeleteBuffers( 1, &cubeVBO );

	return EXIT_SUCCESS;
}

int main2()
{
	sdl::Window app;

	if ( false == app.init( "sample 06", WindowWidth, WindowHeight ) )
	{
		fmt::print( "視窗建立失敗\n" );
		return EXIT_FAILURE;
	}

	auto core = std::make_shared<gl::Core>();

	return main3( app, core );
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
		fmt::print( "異常訊息：{}\n", e.what() );
	}
	catch ( ... )
	{
		fmt::print( "捕獲到未知的異常\n" );
	}

	return result;
}
