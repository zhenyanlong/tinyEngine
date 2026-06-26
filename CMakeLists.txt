# CMakeList.txt: tinyEngine 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.8)

# 如果支持，请为 MSVC 编译器启用热重载。
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()
project (tinyEngine)
add_subdirectory(thirdParty/glfw)
add_subdirectory(thirdParty/glm)
# add Vulkan
find_package(Vulkan REQUIRED)

# imgui不是标准cmake工程，需要手动配置头文件
file(GLOB_RECURSE IMGUI_SRCS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/thirdParty/imgui/*.cpp")

# src 下源码：CONFIGURE_DEPENDS 在增删 .cpp 时会触发重新配置，避免链接缺文件
file(GLOB_RECURSE SOURCE CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(${PROJECT_NAME}
    ${SOURCE}
    ${IMGUI_SRCS}
    thirdParty/ImGuizmo/ImGuizmo.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
                src
                thirdParty/glfw/include
                thirdParty/stb
                thirdParty/tinyobjloader
                thirdParty/imgui
                thirdParty/stb_image
                thirdParty/ImGuizmo
                thirdParty/renderdoc
                thirdParty/json/include
                thirdParty/cgltf
        )


# 获取当前二进制目录到RUNTIME_OUTPUT_DIRECTORY这个变量处，也可以讲 ${CMAKE_BINARY_DIR}替换为"/path/to/your/directory"
set_target_properties(tinyEngine PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# 获取运行时Directory
get_target_property(TARGET_DIR tinyEngine RUNTIME_OUTPUT_DIRECTORY)
# 添加预处理命令
add_definitions(-DGLEW_STATIC)

# res/ 不再复制到 exe 旁。applicationResourceRoot() 从 exe 路径向上查找项目根，
# 所有 res 读写（mesh/anim/scene/texture/shader）都以项目根/res/ 为唯一基准。

target_link_libraries(${PROJECT_NAME}
        Vulkan::Vulkan
        glfw ${GLFW_LIBRARIES}
        glm::glm
        )

target_compile_definitions(${PROJECT_NAME} PRIVATE
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_ENABLE_EXPERIMENTAL
        GLFW_INCLUDE_VULKAN
        STB_IMAGE_IMPLEMENTATION
        TINYOBJLOADER_IMPLEMENTATION
        )
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET tinyEngine PROPERTY CXX_STANDARD 20)
endif()


# TODO: 如有需要，请添加测试并安装目标。
