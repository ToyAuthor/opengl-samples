#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <glad/glad.h>
#include <fmt/core.h>
#include "gl/ImageData.hpp"
#include "gl/CreateImage.hpp"
#include <cstring>
#include <cstdint>
#include "gl/StreamingBuffer.hpp"

namespace gl{

/*
 * 紋理陣列管理器：
 *
 * 使用 GL_TEXTURE_2D_ARRAY 把多張同尺寸圖片包成同一張紋理
 * 透過 layer index 存取不同圖片，避免頻繁切換 texture binding
 *
 * 搭配 ARB_bindless_texture
 * 取得 64-bit 的 Texture Handle 並設為常駐(resident)
 * shader 端不需要 glBindTexture / glActiveTexture
 * 直接透過 uniform 傳遞 handle 即可取樣
 * 非常適合搭配 MultiDrawIndirect
 * 多個 draw 共用同一份 handle
 * 靠 layer index 區分要顯示哪張圖片
 * 完全不必為了切材質而拆成多次 draw call
 */
class TextureManager
{
	public:

		TextureManager() = default;

		~TextureManager()
		{
			release();
		}

		TextureManager( const TextureManager& ) = delete;
		TextureManager& operator=( const TextureManager& ) = delete;

		TextureManager( TextureManager&& rhs ) noexcept
		{
			moveFrom( std::move( rhs ) );
		}

		TextureManager& operator=( TextureManager&& rhs ) noexcept
		{
			if ( this != &rhs )
			{
				release();
				moveFrom( std::move( rhs ) );
			}

			return *this;
		}

		// 一般用法：內部自行以同步方式上傳（保留原有介面相容性）
		/*bool build( const std::vector<std::string>& names, int width = 128, int height = 128 )
		{
			if ( !allocate( static_cast<int>( names.size() ), width, height ) )
			{
				return false;
			}

			for ( size_t i = 0; i < names.size(); ++i )
			{
				ImageData img;
				img.width  = width;
				img.height = height;
				CreateImage( names[i], img );

				glTextureSubImage3D(
					_textureId, 0,
					0, 0, static_cast<GLint>( i ),
					width, height, 1,
					GL_RGBA, GL_UNSIGNED_BYTE,
					img.pixels.data() );
			}

			return finalize();
		}*/

		// -------- PBO 上傳流程：allocate() -> uploadLayer() x N -> finalize() --------

		// 只建立 texture array 的 immutable storage，不上傳任何像素
		bool allocate( int layers, int width, int height )
		{
			release();

			if ( layers <= 0 || width <= 0 || height <= 0 )
			{
				return false;
			}

			_width     = width;
			_height    = height;
			_layerCount = layers;

			const int mipLevels = 1 + static_cast<int>(
				std::floor( std::log2( static_cast<float>( std::max( width, height ) ) ) ) );

			glCreateTextures( GL_TEXTURE_2D_ARRAY, 1, &_textureId );
			glTextureStorage3D( _textureId, mipLevels, GL_RGBA8, width, height, layers );

			return _textureId != 0;
		}

		// 透過外部提供的 StreamingBuffer(PBO) 非同步上傳單一 layer
		//
		// 流程：
		//   beginWrite()  取得目前槽位的 mapped 指標（內部以 fence 等待 GPU 用完該槽）
		//   memcpy        CPU 直接寫入，不需 glBufferSubData
		//   glBindBuffer  綁定 GL_PIXEL_UNPACK_BUFFER，pixels 參數改成槽位的 byte offset
		//   endWrite()    插入 fence 並切換到下一個槽位，CPU 不必等待 GPU
		bool uploadLayer( StreamingBuffer& pbo, int layer, const ImageData& img )
		{
			if ( _textureId == 0 || layer < 0 || layer >= _layerCount )
			{
				return false;
			}

			if ( img.width != _width || img.height != _height )
			{
				fmt::print( "TextureManager: 圖片尺寸不一致，layer {} 略過\n", layer );
				return false;
			}

			const size_t bytes = static_cast<size_t>( _width ) * _height * 4; // RGBA8

			if ( pbo.getTarget() != GL_PIXEL_UNPACK_BUFFER || pbo.getSlotSize() < bytes )
			{
				fmt::print( "TextureManager: PBO 槽位不足或 target 不正確\n" );
				return false;
			}

			void* dst = pbo.beginWrite();

			if ( dst == nullptr )
			{
				return false;
			}

			std::memcpy( dst, img.pixels.data(), bytes );

			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, pbo.getBufferId() );

			glTextureSubImage3D(
				_textureId,
				0,                       // mip level 0
				0, 0, layer,             // xoffset, yoffset, zoffset(=layer)
				_width, _height, 1,      // width, height, depth(=1 layer)
				GL_RGBA, GL_UNSIGNED_BYTE,
				reinterpret_cast<const void*>(
					static_cast<uintptr_t>( pbo.getCurrentOffset() ) ) );

			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );

			// fence 必須在 draw/transfer 指令送出後才插入，才能正確保護該槽位
			pbo.endWrite();

			return true;
		}

		// 產生 mipmap、設定取樣參數、取得 bindless handle 並設為常駐
		bool finalize()
		{
			if ( _textureId == 0 )
			{
				return false;
			}

			glGenerateTextureMipmap( _textureId );

			glTextureParameteri( _textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
			glTextureParameteri( _textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glTextureParameteri( _textureId, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTextureParameteri( _textureId, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

			_handle = glGetTextureHandleARB( _textureId );

			if ( _handle == 0 )
			{
				fmt::print( "TextureManager: 取得 Bindless Texture Handle 失敗（驅動可能不支援 ARB_bindless_texture）\n" );
				return false;
			}

			glMakeTextureHandleResidentARB( _handle );

			return true;
		}

		bool isValid() const
		{
			return _textureId != 0 && _handle != 0;
		}

		GLuint    getTextureId() const  { return _textureId; }
		// 取得 Bindless Texture Handle
		GLuint64  getHandle() const     { return _handle; }
		int       getLayerCount() const { return _layerCount; }

	private:

		void release()
		{
			if ( _handle != 0 )
			{
				glMakeTextureHandleNonResidentARB( _handle );
				_handle = 0;
			}

			if ( _textureId != 0 )
			{
				glDeleteTextures( 1, &_textureId );
				_textureId = 0;
			}

			_layerCount = 0;
			_width      = 0;
			_height     = 0;
		}

		void moveFrom( TextureManager&& rhs ) noexcept
		{
			_textureId  = rhs._textureId;
			_handle     = rhs._handle;
			_layerCount = rhs._layerCount;

			rhs._textureId  = 0;
			rhs._handle     = 0;
			rhs._layerCount = 0;
		}

		GLuint   _textureId  = 0;   // 紀錄 texture array 的 ID
		GLuint64 _handle     = 0;   // 紀錄 Bindless Texture Handle
		int      _layerCount = 0;
		int      _width      = 0;
		int      _height     = 0;
};

}
