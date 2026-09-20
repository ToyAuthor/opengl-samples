#pragma once

namespace sdl{

// 回傳時戳，單位是秒
inline float GetTick()
{
	return static_cast<float>( SDL_GetTicks() ) / 1000.0f;
}

}
