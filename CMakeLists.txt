cmake_minimum_required (VERSION 2.6)
project (hw-gl)

find_package(OpenGL REQUIRED)
#find_package(glfw3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)
pkg_search_module(GLEW REQUIRED glew)

include_directories(glad)


#find_package(glm)
include_directories( ${OPENGL_INCLUDE_DIRS}  ${GLFW_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS} )

#set(GLAD_GL "glad/gl.h"})

#if(CMAKE_COMPILER_IS_GNUCXX)
#    add_definitions(-std=gnu++11)
#endif()


# trans: swgl-1
add_executable(trans main.cpp)
target_link_libraries(trans ${OPENGL_LIBRARIES} ${GLEW_LIBRARIES} ${GLFW_LIBRARIES})

