#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <glad/glad.h>
#include <fmt/core.h>
#include "gl/ImageData.hpp"
#include "gl/CreateImage.hpp"

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

		// 依照 names 清單建立 texture array，每個字串各佔一個 layer
		// width / height 可自訂該組 texture array 的尺寸，
		// 不同的 TextureManager 實例可以各自使用不同尺寸，彼此互不影響
		bool build( const std::vector<std::string>& names, int width = 128, int height = 128 )
		{
			release();

			if ( names.empty() )
			{
				return false;
			}

			std::vector<ImageData> images( names.size() );

			for ( size_t i = 0; i < names.size(); ++i )
			{
				images[i].width  = width;
				images[i].height = height;
				CreateImage( names[i], images[i] );
			}

			const int layers    = static_cast<int>( images.size() );
			const int mipLevels = 1 + static_cast<int>( std::floor( std::log2( static_cast<float>( std::max( width, height ) ) ) ) );

			// 使用 GL_TEXTURE_2D_ARRAY 把多張同尺寸圖片包成同一張紋理
			glCreateTextures( GL_TEXTURE_2D_ARRAY, 1, &_textureId );

			// 一次配置好整個 mipmap chain 所需的儲存空間（Immutable Storage）
			glTextureStorage3D( _textureId, mipLevels, GL_RGBA8, width, height, layers );

			for ( int layer = 0; layer < layers; ++layer )
			{
				const ImageData& img = images[layer];

				if ( img.width != width || img.height != height )
				{
					fmt::print( "TextureManager: 圖片尺寸不一致，layer {} 略過\n", layer );
					continue;
				}

				glTextureSubImage3D(
					_textureId,
					0,                 // mip level 0
					0, 0, layer,       // xoffset, yoffset, zoffset(=layer)
					width, height, 1,  // width, height, depth(=1 layer)
					GL_RGBA, GL_UNSIGNED_BYTE,
					img.pixels.data() );
			}

			glGenerateTextureMipmap( _textureId );

			glTextureParameteri( _textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
			glTextureParameteri( _textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glTextureParameteri( _textureId, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTextureParameteri( _textureId, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

			// 取得 Bindless Texture Handle 並設為常駐（resident），
			// 之後 shader 端只要拿到 handle 即可取樣，不需要 glBindTexture
			_handle = glGetTextureHandleARB( _textureId );

			if ( _handle == 0 )
			{
				fmt::print( "TextureManager: 取得 Bindless Texture Handle 失敗（驅動可能不支援 ARB_bindless_texture）\n" );
				return false;
			}

			glMakeTextureHandleResidentARB( _handle );

			_layerCount = layers;

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
		GLuint64 _handle     = 0;   // 紀錄 Bindless Texture Handle，也是種 ID，讓 shader 端可以直接選擇要使用什麼 texture
		int      _layerCount = 0;
};

}
