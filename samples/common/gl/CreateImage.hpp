#pragma once

#include <string>
#include "gl/ImageData.hpp"

namespace gl{

// 圖檔產生器
// 按照 name 這個字串的內容來隨便產生一張圖片
inline void CreateImage( const std::string& name, ::gl::ImageData& img )
{
	img.width    = 128;
	img.height   = 128;
	img.channels = 4;
	img.pixels.resize( img.width * img.height * 4 );

	uint32_t seed = 2166136261u;

	for ( char c : name )
	{
		seed ^= static_cast<unsigned char>( c );
		seed *= 16777619u;
	}

	unsigned char r = static_cast<unsigned char>( ( seed >>  0 ) & 255 );
	unsigned char g = static_cast<unsigned char>( ( seed >>  8 ) & 255 );
	unsigned char b = static_cast<unsigned char>( ( seed >> 16 ) & 255 );

	for ( int y = 0; y < img.height; ++y )
	{
		for ( int x = 0; x < img.width; ++x )
		{
			bool checker = ( ( x / 16 ) + ( y / 16 ) ) % 2 == 0;
			int  i       = ( y * img.width + x ) * 4;

			img.pixels[i + 0] = checker ? r : 255;
			img.pixels[i + 1] = checker ? g : 255;
			img.pixels[i + 2] = checker ? b : 255;
			img.pixels[i + 3] = 255;
		}
	}
}

}
