project(sample03)

set(INC_DIR
	${opengl_samples_SOURCE_DIR}/extlibs/include
	${opengl_samples_SOURCE_DIR}/samples/common
	${opengl_samples_LINK_DIRS}
)

set(SOURCE
	sample03.cpp
	${opengl_samples_SOURCE_DIR}/samples/common/gl/StreamingBuffer.cpp
	${opengl_samples_SOURCE_DIR}/extlibs/src/glad/glad.c
)

set(LINK_LIB
	fmt
	sdl2
)

if(UNIX)
	list(APPEND LINK_LIB GL GLU X11)
elseif(WIN32)
	list(APPEND LINK_LIB opengl32 glu32 gdi32)
endif()

make_opengl_binary(sample03
	INC_DIRS    ${INC_DIR}
	SOURCES     ${SOURCE}
	LINK_LIBS   ${LINK_LIB}
	LINK_DIRS   ${opengl_samples_LINK_DIRS}
)
