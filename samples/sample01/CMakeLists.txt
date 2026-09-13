project(sample01)

# 編譯程式所必需的 header file，就放置在這些資料夾裡
set(INC_DIR
	${opengl_samples_SOURCE_DIR}/extlibs/include
	${opengl_samples_SOURCE_DIR}/samples/common
	${opengl_samples_LINK_DIRS}
)

# 編譯程式所必需連結的 library 名稱
# library 資料夾位置寫在 opengl_samples_LINK_DIRS 上面了
set(LINK_LIB
	glew
	fmt
	sdl2
)

# 這些是作業系統提供的 library
# Linux 與 Windows 平台所需要添加的 library 不一樣
if(UNIX)
	list(APPEND LINK_LIB GL GLU X11)
elseif(WIN32)
	list(APPEND LINK_LIB opengl32 glu32 gdi32)
endif()

make_opengl_binary(sample01
	INC_DIRS    ${INC_DIR}
	SOURCES     sample01.cpp
	LINK_LIBS   ${LINK_LIB}
	LINK_DIRS   ${opengl_samples_LINK_DIRS}
)
