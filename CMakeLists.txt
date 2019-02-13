cmake_minimum_required(VERSION 3.11)
project(vulkanfft VERSION 0.0.1 DESCRIPTION "Fast Fourier Transform using the Vulkan API")
find_package(Vulkan REQUIRED)

add_custom_command(OUTPUT shaderModuleCode
    COMMAND ${VULKAN_SDK}/bin/glslangValidator -V -e radix2 -DRADIX=2 -o fft.spv --source-entrypoint main ${CMAKE_SOURCE_DIR}/src/fft.comp
    COMMAND hexdump -v -e '/4 \"0x%08X, \"' fft.spv > shaderModuleCode
    DEPENDS ${CMAKE_SOURCE_DIR}/src/fft.comp
)
add_custom_target(ShaderModuleCode DEPENDS shaderModuleCode)

add_library(ObjectLibrary OBJECT src/VulkanFFT.c)
add_dependencies(ObjectLibrary ShaderModuleCode)
include_directories(ObjectLibrary PRIVATE include ${CMAKE_BINARY_DIR})
set_property(TARGET ObjectLibrary PROPERTY POSITION_INDEPENDENT_CODE ON)
add_library(StaticLibrary STATIC $<TARGET_OBJECTS:ObjectLibrary>)
add_library(SharedLibrary SHARED $<TARGET_OBJECTS:ObjectLibrary>)
set_target_properties(StaticLibrary SharedLibrary PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
target_link_libraries(SharedLibrary ${Vulkan_LIBRARIES})

find_package(PNG 1.6.0)
if(${PNG_FOUND})
    add_executable(CLI src/cli.c)
    set_target_properties(CLI PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    target_link_libraries(CLI SharedLibrary)
    target_link_libraries(CLI ${Vulkan_LIBRARIES})
    target_link_libraries(CLI ${PNG_LIBRARY})
endif()

install(TARGETS StaticLibrary SharedLibrary
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include) # include/${PROJECT_NAME}
