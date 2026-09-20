#pragma once

// 這文件專做一些零碎的小功能

#include <glad/glad.h>

namespace gl{

void ClearScreen()
{
	constexpr GLfloat clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
	glClearNamedFramebufferfv( 0, GL_COLOR, 0, clearColor );
}

}
