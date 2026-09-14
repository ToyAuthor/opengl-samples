
function(make_opengl_binary _name)
	set(multiValueArgs INC_DIRS SOURCES LINK_LIBS LINK_DIRS)

	foreach(arg ${multiValueArgs})
		set(_OGL_${arg} "")
	endforeach()

	cmake_parse_arguments(_OGL "" "" "${multiValueArgs}" ${ARGN})

	if(_OGL_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "Unknown arguments in make_opengl_binary: ${_OGL_UNPARSED_ARGUMENTS}")
	endif()

	add_executable(${_name} ${_OGL_SOURCES})

	if(_OGL_INC_DIRS)
		target_include_directories(${_name} PRIVATE ${_OGL_INC_DIRS})
	endif()

	if(_OGL_LINK_DIRS)
		target_link_directories(${_name} PRIVATE ${_OGL_LINK_DIRS})
	endif()

	if(_OGL_LINK_LIBS)
		target_link_libraries(${_name} PRIVATE ${_OGL_LINK_LIBS})
	endif()

	set(OGL_OUTPUT_PATH ${opengl_samples_BINARY_DIR}/bin)

	if(MSVC)
		set_target_properties(${_name} PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY            ${OGL_OUTPUT_PATH}
			RUNTIME_OUTPUT_DIRECTORY_DEBUG      ${OGL_OUTPUT_PATH}
			RUNTIME_OUTPUT_DIRECTORY_RELEASE    ${OGL_OUTPUT_PATH}
			ARCHIVE_OUTPUT_DIRECTORY            ${OGL_OUTPUT_PATH}
			ARCHIVE_OUTPUT_DIRECTORY_DEBUG      ${OGL_OUTPUT_PATH}
			ARCHIVE_OUTPUT_DIRECTORY_RELEASE    ${OGL_OUTPUT_PATH}
		)
	else()
		set_target_properties(${_name} PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "$<0:>${OGL_OUTPUT_PATH}"
			LIBRARY_OUTPUT_DIRECTORY "$<0:>${OGL_OUTPUT_PATH}"
			ARCHIVE_OUTPUT_DIRECTORY "$<0:>${OGL_OUTPUT_PATH}"
		)

		if(WIN32)
			set_target_properties(${_name} PROPERTIES LINK_FLAGS "-shared-libgcc -shared-libstdc++")
		endif()
	endif()

	target_compile_options(${_name} PRIVATE
		$<$<CXX_COMPILER_ID:MSVC>:/utf-8>
		$<$<C_COMPILER_ID:MSVC>:/utf-8>
	)

	target_compile_features(${_name} PUBLIC cxx_std_20)
endfunction()
