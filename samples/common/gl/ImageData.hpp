#pragma once

#include <vector>

namespace gl{

// 用來儲存圖檔
struct ImageData
{
	int width    = 128;
	int height   = 128;
	// 所有材質都統一使用 RGBA 格式就好，反正顯卡也總是會將 RGB 轉成 RGBA
	constexpr static int channels = 4;
	std::vector<unsigned char> pixels;
};

}
