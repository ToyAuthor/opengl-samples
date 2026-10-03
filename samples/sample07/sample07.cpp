/*
 * 範例名稱：sample07
 * -----------------------------------------------------------------------------
 * 示範前向渲染(Forward Rendering)的打光方式：
 * 每個物件一個 Draw Call 內同時完成「貼圖取樣」與「Blinn-Phong 打光」，
 * 直接畫進預設 framebuffer，不像 sample05 使用 G-Buffer 做延遲著色。
 *
 *   - 使用 gl::Camera 搭配 FPS 方式操作視角(WASDQE + 滑鼠)
 *   - View / Projection 矩陣透過 gl::StreamingBuffer 以 UBO 非同步上傳
 *
 * 場景內容：一個貼圖立方體 + 一塊貼圖地板，皆受同一個點光源影響
 */

#include <cmath>
#include <string>
#include <vector>
#include <memory>
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
#include "gl/VertexArray.hpp"      // 用來建立 VAO，並管理 VertexAttrib 與 VertexBinding
#include "gl/VertexAttrib.hpp"
#include "gl/ImageData.hpp"
#include "gl/CreateImage.hpp"

namespace{

constexpr int WindowWidth  = 1280;
constexpr int WindowHeight = 720;

//------------------------------------------------------------------------------
// Shader 原始碼
//------------------------------------------------------------------------------

// Vertex Shader：將頂點轉到 clip space，並把 world space 的位置 / 法線帶給 fragment shader 做打光用
const char* VertexShaderSource = R"(
    #version 460 core

    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aNormal;
    layout (location = 2) in vec2 aUV;

    layout (std140, binding = 0) uniform CameraBlock
    {
        mat4 view;
        mat4 projection;
        mat4 viewProjection;
    } camera;

    layout (location = 0) uniform mat4 model;
    layout (location = 1) uniform mat3 normalMatrix;  // world space 的法線矩陣：transpose(inverse(mat3(model)))
    layout (location = 2) uniform vec2 uvScale;        // 讓地板貼圖可以重複貼(tiling)

    layout (location = 0) out vec3 vFragPosWorld;
    layout (location = 1) out vec3 vNormalWorld;
    layout (location = 2) out vec2 vUV;

    void main()
    {
        vec4 worldPos = model * vec4( aPos, 1.0 );

        vFragPosWorld = worldPos.xyz;
        vNormalWorld  = normalize( normalMatrix * aNormal );
        vUV           = aUV * uvScale;

        gl_Position = camera.viewProjection * worldPos;
    }
)";

// Fragment Shader：Blinn-Phong 打光，貼圖當作 albedo
const char* FragmentShaderSource = R"(
    #version 460 core

    layout (location = 0) in vec3 vFragPosWorld;
    layout (location = 1) in vec3 vNormalWorld;
    layout (location = 2) in vec2 vUV;

    layout (location = 0) out vec4 FragColor;

    layout (binding = 0) uniform sampler2D albedoMap;

    layout (location = 3) uniform vec3 lightPosWorld;
    layout (location = 4) uniform vec3 viewPosWorld;
    layout (location = 5) uniform vec3 lightColor;

    void main()
    {
        vec3 albedo = texture( albedoMap, vUV ).rgb;
        vec3 normal = normalize( vNormalWorld );

        // 環境光：避免背光面完全死黑
        vec3 ambient = 0.15 * albedo;

        vec3 lightDir = normalize( lightPosWorld - vFragPosWorld );
        vec3 viewDir  = normalize( viewPosWorld - vFragPosWorld );
        vec3 halfway  = normalize( lightDir + viewDir );

        float diff = max( dot( normal, lightDir ), 0.0 );
        float spec = pow( max( dot( normal, halfway ), 0.0 ), 32.0 );

        // 簡單的距離衰減，光源越遠影響越弱
        float dist  = length( lightPosWorld - vFragPosWorld );
        float atten = 1.0 / ( 1.0 + 0.09 * dist + 0.032 * dist * dist );

        vec3 diffuse  = diff * albedo * lightColor * atten;
        vec3 specular = spec * vec3( 0.4 ) * lightColor * atten;

        vec3 color = ambient + diffuse + specular;

        FragColor = vec4( color, 1.0 );
    }
)";

//------------------------------------------------------------------------------
// 幾何資料
//------------------------------------------------------------------------------

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// 依照法線與切線方向產生一個面(兩個三角形)，立方體與地板都靠它組裝
void AppendFace(
    std::vector<Vertex>& out,
    const glm::vec3&     normal,
    const glm::vec3&     tangent,
    float                halfSize,
    float                uvRepeat = 1.0f )
{
    const glm::vec3 bitangent = glm::cross( normal, tangent );
    const glm::vec3 center    = normal * halfSize;

    const glm::vec3 p0 = center - tangent * halfSize - bitangent * halfSize;
    const glm::vec3 p1 = center + tangent * halfSize - bitangent * halfSize;
    const glm::vec3 p2 = center + tangent * halfSize + bitangent * halfSize;
    const glm::vec3 p3 = center - tangent * halfSize + bitangent * halfSize;

    out.push_back( { p0, normal, { 0.0f,     0.0f     } } );
    out.push_back( { p1, normal, { uvRepeat, 0.0f     } } );
    out.push_back( { p2, normal, { uvRepeat, uvRepeat } } );

    out.push_back( { p0, normal, { 0.0f,     0.0f     } } );
    out.push_back( { p2, normal, { uvRepeat, uvRepeat } } );
    out.push_back( { p3, normal, { 0.0f,     uvRepeat } } );
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

std::vector<Vertex> CreateGroundVertices( float halfSize, float uvRepeat )
{
    std::vector<Vertex> vertices;
    vertices.reserve( 6 );

    // 法線朝上的一個大平面，uvRepeat 讓貼圖重複貼滿地板
    AppendFace( vertices, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, halfSize, uvRepeat );

    return vertices;
}

//------------------------------------------------------------------------------
// 軟體產生貼圖
//------------------------------------------------------------------------------

GLuint CreateTexture2D( const ::gl::ImageData& img )
{
    GLuint texture = 0;

    glCreateTextures( GL_TEXTURE_2D, 1, &texture );
    glTextureStorage2D( texture, 1, GL_SRGB8_ALPHA8, img.width, img.height );
    glTextureSubImage2D( texture, 0, 0, 0, img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data() );

    glTextureParameteri( texture, GL_TEXTURE_WRAP_S,     GL_REPEAT );
    glTextureParameteri( texture, GL_TEXTURE_WRAP_T,     GL_REPEAT );
    glTextureParameteri( texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTextureParameteri( texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

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

int main2( sdl::Window& app, std::shared_ptr<gl::Core> core )
{
    if ( false == app.init( "sample 08", WindowWidth, WindowHeight ) )
    {
        fmt::print( "視窗建立失敗\n" );
        return EXIT_FAILURE;
    }

    gl::ShaderProgram myShader( core, VertexShaderSource, FragmentShaderSource );

    // 3D 場景需要深度測試，gl::Core 目前沒有包裝這塊，直接呼叫底層 API
    glEnable( GL_DEPTH_TEST );

    //--------------------------------------------------------------------------
    // 幾何：立方體與地板共用同一個 VBO，靠 offset 區分
    //--------------------------------------------------------------------------

    const std::vector<Vertex> cubeVertices   = CreateCubeVertices();
    const std::vector<Vertex> groundVertices = CreateGroundVertices( 10.0f, 8.0f );

    std::vector<Vertex> allVertices;
    allVertices.reserve( cubeVertices.size() + groundVertices.size() );
    allVertices.insert( allVertices.end(), cubeVertices.begin(),   cubeVertices.end() );
    allVertices.insert( allVertices.end(), groundVertices.begin(), groundVertices.end() );

    const GLint cubeFirst   = 0;
    const GLint cubeCount   = static_cast<GLint>( cubeVertices.size() );
    const GLint groundFirst = cubeCount;
    const GLint groundCount = static_cast<GLint>( groundVertices.size() );

    GLuint geometryVBO = 0;
    glCreateBuffers( 1, &geometryVBO );
    glNamedBufferStorage(
        geometryVBO,
        static_cast<GLsizeiptr>( allVertices.size() * sizeof( Vertex ) ),
        allVertices.data(),
        0 );

    // 建立 VAO（改用 gl::VertexArray 包裝，RAII 自動管理生命週期）
    auto VAO = std::make_shared<gl::VertexArray>( core );

    auto attribPos    = std::make_shared<gl::VertexAttrib>( VAO, 0 );  // 位置(location = 0)
    auto attribNormal = std::make_shared<gl::VertexAttrib>( VAO, 1 );  // 法線(location = 1)
    auto attribUV     = std::make_shared<gl::VertexAttrib>( VAO, 2 );  // UV(location = 2)

    attribPos->setFormat   ( 3, GL_FLOAT, GL_FALSE, offsetof( Vertex, position ) );
    attribNormal->setFormat( 3, GL_FLOAT, GL_FALSE, offsetof( Vertex, normal ) );
    attribUV->setFormat    ( 2, GL_FLOAT, GL_FALSE, offsetof( Vertex, uv ) );

    // 三個屬性都黏到綁定點 0
    VAO->bindingIndex0.bind( attribPos );
    VAO->bindingIndex0.bind( attribNormal );
    VAO->bindingIndex0.bind( attribUV );

    VAO->bindingIndex0.bindVBO( geometryVBO, 0, static_cast<GLsizei>( sizeof( Vertex ) ) );

    //--------------------------------------------------------------------------
    // 貼圖：立方體與地板各自靠 CreateImage 產生不同花紋
    //--------------------------------------------------------------------------

    ::gl::ImageData cubeImage;
    cubeImage.width  = 256;
    cubeImage.height = 256;
    ::gl::CreateImage( "sample07-cube", cubeImage );

    ::gl::ImageData groundImage;
    groundImage.width  = 256;
    groundImage.height = 256;
    ::gl::CreateImage( "sample07-ground", groundImage );

    const GLuint cubeTexture   = CreateTexture2D( cubeImage );
    const GLuint groundTexture = CreateTexture2D( groundImage );

    //--------------------------------------------------------------------------
    // Camera 的 View / Projection 矩陣，沿用 sample07 的非同步上傳手法
    //--------------------------------------------------------------------------

    gl::StreamingBuffer cameraBuffer( GL_UNIFORM_BUFFER, sizeof( gl::Camera::UniformBlock ), 3 );

    if ( !cameraBuffer.isValid() )
    {
        fmt::print( "StreamingBuffer 建立失敗(驅動可能不支援 ARB_buffer_storage)\n" );
        return EXIT_FAILURE;
    }

    // 攝影機初始位置往後退、略為抬高，才看得到地板上的立方體
    gl::Camera camera( glm::vec3( 0.0f, 1.5f, 5.0f ) );

    // 光源固定位置，之後也可以隨時間移動做出動態光影
    const glm::vec3 lightPos   = glm::vec3( 3.0f, 4.0f, 2.0f );
    const glm::vec3 lightColor = glm::vec3( 1.0f, 1.0f, 0.95f );

    bool       quit = false;
    SDL_Event  event;
    float      lastTick = sdl::GetTick();

    while ( false == quit )
    {
        const float currentTick = sdl::GetTick();
        const float deltaTime   = currentTick - lastTick;
        lastTick = currentTick;

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

        core->clear();

        // Core::clear() 只清色彩緩衝，深度緩衝要另外清
        const float depthClearValue = 1.0f;
        glClearNamedFramebufferfv( 0, GL_DEPTH, 0, &depthClearValue );

        // 非同步上傳 Camera 矩陣至 UBO，並綁定到 binding = 0
        const float aspectRatio = static_cast<float>( WindowWidth ) / static_cast<float>( WindowHeight );
        camera.uploadToUBO( cameraBuffer, aspectRatio, 0 );

        // 共用的燈光 / 攝影機位置 uniform(location = 3, 4, 5)
        const glm::vec3 viewPos = camera.getPosition();

        glProgramUniform3fv( myShader.getID(), 3, 1, glm::value_ptr( lightPos ) );
        glProgramUniform3fv( myShader.getID(), 4, 1, glm::value_ptr( viewPos ) );
        glProgramUniform3fv( myShader.getID(), 5, 1, glm::value_ptr( lightColor ) );

        myShader.use();
        VAO->bind();

        // 立方體：讓它緩慢自轉，打光的立體感才看得明顯
        {
            const float timeValue = sdl::GetTick();

            glm::mat4 model = glm::mat4( 1.0f );
            model = glm::rotate( model, timeValue * 0.5f, glm::vec3( 0.0f, 1.0f, 0.0f ) );

            const glm::mat3 normalMatrix = glm::transpose( glm::inverse( glm::mat3( model ) ) );
            const glm::vec2 uvScale( 1.0f, 1.0f );

            glProgramUniformMatrix4fv( myShader.getID(), 0, 1, GL_FALSE, glm::value_ptr( model ) );
            glProgramUniformMatrix3fv( myShader.getID(), 1, 1, GL_FALSE, glm::value_ptr( normalMatrix ) );
            glProgramUniform2fv      ( myShader.getID(), 2, 1, glm::value_ptr( uvScale ) );

            glBindTextureUnit( 0, cubeTexture );
            glDrawArrays( GL_TRIANGLES, cubeFirst, cubeCount );
        }

        // 地板：固定不動
        {
            const glm::mat4 model = glm::mat4( 1.0f );
            const glm::mat3 normalMatrix = glm::transpose( glm::inverse( glm::mat3( model ) ) );
            const glm::vec2 uvScale( 1.0f, 1.0f );  // uvRepeat 已經在頂點資料裡算好了

            glProgramUniformMatrix4fv( myShader.getID(), 0, 1, GL_FALSE, glm::value_ptr( model ) );
            glProgramUniformMatrix3fv( myShader.getID(), 1, 1, GL_FALSE, glm::value_ptr( normalMatrix ) );
            glProgramUniform2fv      ( myShader.getID(), 2, 1, glm::value_ptr( uvScale ) );

            glBindTextureUnit( 0, groundTexture );
            glDrawArrays( GL_TRIANGLES, groundFirst, groundCount );
        }

        app.refresh();
    }

    glDeleteTextures( 1, &cubeTexture );
    glDeleteTextures( 1, &groundTexture );
    glDeleteBuffers( 1, &geometryVBO );

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
