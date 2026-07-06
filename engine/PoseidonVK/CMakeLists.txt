file(GLOB_RECURSE POSEIDON_VK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

set(POSEIDON_VK_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/BufferVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BootstrapPushConstantsVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DescriptorBindingsVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DrawConstantsVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/EngineVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameConstantsVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GraphicsBackendVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderStateVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SceneDrawCommandsVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ScenePushConstantsVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VertexLayoutVK.hpp
)

set(POSEIDON_VK_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/bootstrap_triangle.vert.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/bootstrap_triangle.frag.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/scene.vert.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/scene.frag.glsl
)

set(POSEIDON_VK_GENERATED_SHADER_HEADERS)
foreach(shader_file IN LISTS POSEIDON_VK_SHADER_SOURCES)
    get_filename_component(shader_name "${shader_file}" NAME)
    set(shader_header "${CMAKE_BINARY_DIR}/generated/PoseidonVK/Shaders/${shader_name}.hpp")
    add_custom_command(
        OUTPUT ${shader_header}
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${shader_file}
            -DOUTPUT_FILE=${shader_header}
            -P ${CMAKE_SOURCE_DIR}/cmake/GenerateShaderHeader.cmake
        DEPENDS ${shader_file} ${CMAKE_SOURCE_DIR}/cmake/GenerateShaderHeader.cmake
        VERBATIM
    )
    list(APPEND POSEIDON_VK_GENERATED_SHADER_HEADERS ${shader_header})
endforeach()

add_custom_target(PoseidonVKShaderHeaders DEPENDS ${POSEIDON_VK_GENERATED_SHADER_HEADERS})

add_library(PoseidonVK STATIC
    ${POSEIDON_VK_SOURCES}
    ${POSEIDON_VK_HEADERS}
    ${POSEIDON_VK_SHADER_SOURCES}
    ${POSEIDON_VK_GENERATED_SHADER_HEADERS}
)

set_source_files_properties(${POSEIDON_VK_SHADER_SOURCES} PROPERTIES HEADER_FILE_ONLY ON)
add_dependencies(PoseidonVK PoseidonVKShaderHeaders)

find_package(Vulkan REQUIRED)
find_package(glslang CONFIG REQUIRED)

target_link_libraries(PoseidonVK PUBLIC
    Poseidon
    Vulkan::Vulkan
)

target_link_libraries(PoseidonVK PRIVATE
    glslang::glslang
    glslang::glslang-default-resource-limits
    glslang::SPIRV
)

target_include_directories(PoseidonVK PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_definitions(PoseidonVK PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)

if(WIN32)
    target_compile_definitions(PoseidonVK PRIVATE
        __MSC__=1100
        _WINDOWS
    )
endif()

if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    target_compile_options(PoseidonVK PRIVATE
        /GR /Oi /GF
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Ob1>
        $<$<CONFIG:Debug>:/MDd>
        /permissive
    )
endif()

set_target_properties(PoseidonVK PROPERTIES
    OUTPUT_NAME "PoseidonVK"
)
