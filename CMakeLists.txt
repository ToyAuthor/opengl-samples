# 確認你使用的 CMake 版本號大於 3.15 就行
cmake_minimum_required(VERSION 3.15)

project(opengl_samples LANGUAGES C CXX)

# 這裡的設置對 GCC 有用，但是對 Visual Studio 就無法控制了
if(NOT CMAKE_BUILD_TYPE)
#	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
	set(CMAKE_BUILD_TYPE "Debug"   CACHE STRING "Choose the type of build" FORCE)
	# 讓 CMake GUI 的 Debug/Release 欄位擁有下拉式選單
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

# 讓 CMake 替你選擇此開發環境所需要的函式庫版本
# 如果沒有適合你的版本，那就必須請你自行準備了
if(MSVC)
	set(opengl_samples_LINK_DIRS ${PROJECT_SOURCE_DIR}/extlibs/lib/msvc2022_x64_release)
elseif(WIN32)
	set(opengl_samples_LINK_DIRS ${PROJECT_SOURCE_DIR}/extlibs/lib/mingw_x64_release)
#elseif(UNIX)
#	set(opengl_samples_LINK_DIRS ${PROJECT_SOURCE_DIR}/extlibs/lib/gcc_x64_release)
else()
	message(FATAL_ERROR "Binary libraries for this build environment are not yet available.")
endif()

# 將 dll 檔案複製到程式放置的資料夾
if(WIN32)
    file(GLOB TEMP_DLL_LIST "${opengl_samples_LINK_DIRS}/*.dll")
    file(COPY ${TEMP_DLL_LIST} DESTINATION "${PROJECT_BINARY_DIR}/bin")
endif()

# make_opengl_binary 是個函式，用來簡化步驟
include("${PROJECT_SOURCE_DIR}/cmake/make_opengl_binary.cmake" REQUIRED)

add_subdirectory(samples)
