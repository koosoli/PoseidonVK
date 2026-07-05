file(GLOB_RECURSE POSEIDON_VK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

set(POSEIDON_VK_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/EngineVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameConstantsVK.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GraphicsBackendVK.hpp
)

set(POSEIDON_VK_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/bootstrap_triangle.vert.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/bootstrap_triangle.frag.glsl
)

add_library(PoseidonVK STATIC
    ${POSEIDON_VK_SOURCES}
    ${POSEIDON_VK_HEADERS}
    ${POSEIDON_VK_SHADER_SOURCES}
)

set_source_files_properties(${POSEIDON_VK_SHADER_SOURCES} PROPERTIES HEADER_FILE_ONLY ON)

find_package(Vulkan REQUIRED)

target_link_libraries(PoseidonVK PUBLIC
    Poseidon
    Vulkan::Vulkan
)

target_include_directories(PoseidonVK PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
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
