# Copyright OpenFX and contributors to the OpenFX project.
# SPDX-License-Identifier: BSD-3-Clause

# ComfyUI Plugin Test Suite

message(STATUS "Configuring ComfyUI test suite")

# Test executable for ComfyUI client
add_executable(test_comfyui_client
    test_client.cpp
)

target_link_libraries(test_comfyui_client PRIVATE
    ComfyUICommon
    OfxSupport
    nlohmann_json::nlohmann_json
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_include_directories(test_comfyui_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${OPENFX_SOURCE_DIR}/Support/include
)

# Test executable for image I/O
add_executable(test_image_io
    test_image_io.cpp
)

target_link_libraries(test_image_io PRIVATE
    ComfyUICommon
    OfxSupport
    tinyexr::tinyexr
    miniz::miniz
)

target_include_directories(test_image_io PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${OPENFX_SOURCE_DIR}/Support/include
)

# Force arm64 only for test to match Conan packages
if(APPLE)
    set_target_properties(test_image_io PROPERTIES
        OSX_ARCHITECTURES "arm64"
    )
endif()

# Test executable for EXR validation
add_executable(test_exr_validation
    test_exr_validation.cpp
)

target_link_libraries(test_exr_validation PRIVATE
    ComfyUICommon
    OfxSupport
    tinyexr::tinyexr
    miniz::miniz
)

target_include_directories(test_exr_validation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${OPENFX_SOURCE_DIR}/Support/include
)

# Force arm64 only for test to match Conan packages
if(APPLE)
    set_target_properties(test_exr_validation PROPERTIES
        OSX_ARCHITECTURES "arm64"
    )
endif()

# Test executable for base plugin workflow
add_executable(test_base_plugin
    test_base_plugin.cpp
)

target_link_libraries(test_base_plugin PRIVATE
    ComfyUICommon
    OfxSupport
    nlohmann_json::nlohmann_json
    tinyexr::tinyexr
    miniz::miniz
)

target_include_directories(test_base_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${OPENFX_SOURCE_DIR}/Support/include
)

# Force arm64 only for test to match Conan packages
if(APPLE)
    set_target_properties(test_base_plugin PROPERTIES
        OSX_ARCHITECTURES "arm64"
    )
endif()

# Test executable for SAM integration (requires live server)
add_executable(test_sam_integration
    test_sam_integration.cpp
)

target_link_libraries(test_sam_integration PRIVATE
    ComfyUICommon
    OfxSupport
    nlohmann_json::nlohmann_json
    httplib::httplib
    ixwebsocket::ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    tinyexr::tinyexr
    miniz::miniz
)

target_include_directories(test_sam_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${OPENFX_SOURCE_DIR}/Support/include
)

# Force arm64 only for test to match Conan packages
if(APPLE)
    set_target_properties(test_sam_integration PROPERTIES
        OSX_ARCHITECTURES "arm64"
    )
endif()

# Note: Tests are built but not added to CTest by default
# Run manually: ./build/Release/contrib/plugins/ComfyUI/tests/test_comfyui_client
# Run manually: ./build/Release/contrib/plugins/ComfyUI/tests/test_image_io
# Run manually: ./build/Release/contrib/plugins/ComfyUI/tests/test_exr_validation [file.exr]
# Run manually: ./build/Release/contrib/plugins/ComfyUI/tests/test_base_plugin
# Run manually: ./build/Release/contrib/plugins/ComfyUI/tests/test_sam_integration (requires live server)

message(STATUS "ComfyUI test executables configured: test_comfyui_client, test_image_io, test_exr_validation, test_base_plugin, test_sam_integration")
