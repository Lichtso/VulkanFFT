cmake_minimum_required(VERSION 3.11)
set(CMAKE_CXX_STANDARD 11)
project(vulkanfft VERSION 0.0.1 DESCRIPTION "Fast Fourier Transform using the Vulkan API")
find_package(Vulkan REQUIRED)

add_library(ObjectLibrary OBJECT src/VulkanFFT.c)
include_directories(ObjectLibrary PRIVATE include ${CMAKE_BINARY_DIR})
set_property(TARGET ObjectLibrary PROPERTY POSITION_INDEPENDENT_CODE ON)
add_library(StaticLibrary STATIC $<TARGET_OBJECTS:ObjectLibrary>)
add_library(SharedLibrary SHARED $<TARGET_OBJECTS:ObjectLibrary>)
set_target_properties(StaticLibrary SharedLibrary PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
target_link_libraries(SharedLibrary ${Vulkan_LIBRARIES})
foreach(radix 2 4 8)
    add_custom_command(OUTPUT radix${radix}.hex
        COMMAND ${VK_TOOLS}/glslangValidator -V -Os -DRADIX=${radix} -o radix${radix}.spv ${CMAKE_SOURCE_DIR}/src/fft.comp
        COMMAND hexdump -v -e '/4 \"0x%08X, \"' radix${radix}.spv > radix${radix}.hex
        DEPENDS ${CMAKE_SOURCE_DIR}/src/fft.comp
    )
    add_custom_target(radix${radix}ShaderModuleTarget DEPENDS radix${radix}.hex)
    add_dependencies(ObjectLibrary radix${radix}ShaderModuleTarget)
endforeach()

add_executable(CLI src/cli.cpp)
set_target_properties(CLI PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
target_link_libraries(CLI SharedLibrary ${Vulkan_LIBRARIES})

find_package(PNG 1.6.0)
if(${PNG_FOUND})
    target_compile_definitions(CLI PRIVATE HAS_PNG)
    target_link_libraries(CLI ${PNG_LIBRARY})
endif()

find_library(OPENEXR_ILMIMF_LIBRARY NAMES IlmImf)
if(OPENEXR_ILMIMF_LIBRARY)
  target_compile_definitions(CLI PRIVATE HAS_EXR)
  target_link_libraries(CLI ${OPENEXR_ILMIMF_LIBRARY})
endif()

install(TARGETS StaticLibrary SharedLibrary
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include) # include/${PROJECT_NAME}
