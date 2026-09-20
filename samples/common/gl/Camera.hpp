#pragma once

#include <cstring>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "gl/StreamingBuffer.hpp"

namespace gl{

// 簡易 FPS 風格攝影機
// 負責計算 View / Projection 矩陣，並可透過 StreamingBuffer 以 UBO 形式
// 非同步上傳給 shader(std140 佈局)
class Camera
{
	public:

		enum class Movement
		{
			Forward,
			Backward,
			Left,
			Right,
			Up,
			Down
		};

		// std140 佈局：三個 mat4，總共 192 bytes，彼此皆為 16 bytes 對齊
		// 這個 struct 會被 memcpy 到 StreamingBuffer 的映射記憶體
		// 未來可以擴充，繼續使用同一個綁定點，shader 端也只要改變 UBO struct 的內容即可
		struct UniformBlock
		{
			glm::mat4 view;
			glm::mat4 projection;
			glm::mat4 viewProjection;
		};

		Camera(
			glm::vec3 position = glm::vec3( 0.0f, 0.0f, 3.0f ),
			glm::vec3 worldUp  = glm::vec3( 0.0f, 1.0f, 0.0f ),
			float     yaw      = -90.0f,
			float     pitch    = 0.0f )
			: _position( position )
			, _worldUp( worldUp )
			, _yaw( yaw )
			, _pitch( pitch )
		{
			updateVectors();
		}

		// 鍵盤移動：direction 為移動方向，deltaTime 為每幀秒數，用來讓移動速度與 frame rate 無關
		void processKeyboard( Movement direction, float deltaTime )
		{
			const float velocity = _moveSpeed * deltaTime;

			switch ( direction )
			{
				case Movement::Forward:  _position += _front * velocity; break;
				case Movement::Backward: _position -= _front * velocity; break;
				case Movement::Left:     _position -= _right * velocity; break;
				case Movement::Right:    _position += _right * velocity; break;
				case Movement::Up:       _position += _worldUp * velocity; break;
				case Movement::Down:     _position -= _worldUp * velocity; break;
			}
		}

		// 滑鼠移動：xoffset / yoffset 為滑鼠在螢幕上的相對位移量
		void processMouseMovement( float xoffset, float yoffset, bool constrainPitch = true )
		{
			_yaw   += xoffset * _mouseSensitivity;
			_pitch += yoffset * _mouseSensitivity;

			if ( constrainPitch )
			{
				_pitch = glm::clamp( _pitch, -89.0f, 89.0f );
			}

			updateVectors();
		}

		// 滑鼠滾輪：控制視野角（Zoom）
		void processMouseScroll( float yoffset )
		{
			_fovDeg -= yoffset;
			_fovDeg  = glm::clamp( _fovDeg, 1.0f, 90.0f );
		}

		glm::mat4 getViewMatrix() const
		{
			return glm::lookAt( _position, _position + _front, _up );
		}

		glm::mat4 getProjectionMatrix( float aspectRatio ) const
		{
			return glm::perspective( glm::radians( _fovDeg ), aspectRatio, _nearPlane, _farPlane );
		}

		// 計算並透過 StreamingBuffer 非同步上傳 View / Projection 矩陣至 UBO
		// buffer 需以 GL_UNIFORM_BUFFER 建立，且 slotSize >= sizeof(UniformBlock)
		// bindingIndex 對應 shader 裡 layout(std140, binding = N) 的 N
		// 回傳 false 表示該幀寫入失敗(例如 GPU 尚未釋放槽位)，呼叫端可選擇跳過本次上傳
		bool uploadToUBO( StreamingBuffer& buffer, float aspectRatio, GLuint bindingIndex )
		{
			void* dst = buffer.beginWrite();

			if ( dst == nullptr )
			{
				return false;
			}

			UniformBlock block;
			block.view           = getViewMatrix();
			block.projection     = getProjectionMatrix( aspectRatio );
			block.viewProjection = block.projection * block.view;

			std::memcpy( dst, &block, sizeof( UniformBlock ) );

			buffer.bindRange( bindingIndex );
			buffer.endWrite();

			return true;
		}

		glm::vec3 getPosition() const { return _position; }
		void      setPosition( const glm::vec3& position ) { _position = position; }

		float getMoveSpeed() const { return _moveSpeed; }
		void  setMoveSpeed( float speed ) { _moveSpeed = speed; }

		float getMouseSensitivity() const { return _mouseSensitivity; }
		void  setMouseSensitivity( float sensitivity ) { _mouseSensitivity = sensitivity; }

		float getFov() const { return _fovDeg; }

	private:

		void updateVectors()
		{
			glm::vec3 front;
			front.x = std::cos( glm::radians( _yaw ) ) * std::cos( glm::radians( _pitch ) );
			front.y = std::sin( glm::radians( _pitch ) );
			front.z = std::sin( glm::radians( _yaw ) ) * std::cos( glm::radians( _pitch ) );

			_front = glm::normalize( front );
			_right = glm::normalize( glm::cross( _front, _worldUp ) );
			_up    = glm::normalize( glm::cross( _right, _front ) );
		}

		// 姿態相關
		glm::vec3 _position;
		glm::vec3 _front = glm::vec3( 0.0f, 0.0f, -1.0f );
		glm::vec3 _up{};
		glm::vec3 _right{};
		glm::vec3 _worldUp;

		float _yaw;
		float _pitch;

		// 投影相關
		float _fovDeg    = 45.0f;
		float _nearPlane = 0.1f;
		float _farPlane  = 100.0f;

		// 控制相關
		float _moveSpeed        = 2.5f;
		float _mouseSensitivity = 0.1f;
};

}
