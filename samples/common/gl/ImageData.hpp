#pragma once

#include <vector>

namespace gl{

// 用來儲存圖檔
struct ImageData
{
	int width    = 0;
	int height   = 0;
	int channels = 4;
	std::vector<unsigned char> pixels;
};

}
