# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 CallDisaster

# 单独编译冻结的解析/姿势参考，不链接 Qt、Windows 或整个 Core/RenderSupport。
get_filename_component(_render_core_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_executable(war3-render-core-tests
    "${_render_core_repo}/tests/RenderCoreTests.cpp"
    "${_render_core_repo}/tests/RenderCoreAllocProbe.cpp"
    "${_render_core_repo}/src/Render/Assets/MdxModel.cpp"
    "${_render_core_repo}/src/Render/Assets/MdxPose.cpp")
target_link_libraries(war3-render-core-tests PRIVATE War3RenderCore::CPU)
set_target_properties(war3-render-core-tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(war3-render-core-tests PRIVATE /W4 /WX /permissive- /utf-8 /fp:precise)
else()
    target_compile_options(war3-render-core-tests PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()
foreach(_render_core_group IN ITEMS oracle instances external)
    add_test(NAME render-core-${_render_core_group} COMMAND war3-render-core-tests ${_render_core_group})
    set_tests_properties(render-core-${_render_core_group} PROPERTIES TIMEOUT 60)
endforeach()

# 0.1.12：显式源序节点预览；固定数值、严格门禁、预算/无分配及真实SHA绑定分开验收。
add_executable(war3-render-source-order-tests
    "${_render_core_repo}/tests/SourceOrderPreviewTests.cpp"
    "${_render_core_repo}/tests/RenderCoreAllocProbe.cpp")
target_link_libraries(war3-render-source-order-tests PRIVATE War3RenderCore::CPU)
war3_render_core_options(war3-render-source-order-tests)
foreach(_source_order_group IN ITEMS source-order boundaries budgets)
    add_test(NAME render-source-order-${_source_order_group}
        COMMAND war3-render-source-order-tests ${_source_order_group})
    set_tests_properties(render-source-order-${_source_order_group} PROPERTIES TIMEOUT 60)
endforeach()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME render-source-order-real
        COMMAND "${Python3_EXECUTABLE}" "${_render_core_repo}/tests/SourceOrderPreviewRealTests.py"
            "$<TARGET_FILE:war3-render-source-order-tests>")
    set_tests_properties(render-source-order-real PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()

add_executable(war3-render-core-compact-tests
    "${_render_core_repo}/tests/RenderCoreCompactTests.cpp"
    "${_render_core_repo}/tests/RenderCoreAllocProbe.cpp"
    "${_render_core_repo}/src/Render/Assets/MdxModel.cpp")
target_link_libraries(war3-render-core-compact-tests PRIVATE War3RenderCore::CPU)
set_target_properties(war3-render-core-compact-tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(war3-render-core-compact-tests PRIVATE /W4 /WX /permissive- /utf-8 /fp:precise)
else()
    target_compile_options(war3-render-core-compact-tests PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()
add_test(NAME render-core-compact COMMAND war3-render-core-compact-tests)
set_tests_properties(render-core-compact PROPERTIES TIMEOUT 60)

# 可移植的插件输入校验；不是 GPU/WARP 验收。
add_executable(war3-render-skin-input-tests
    "${_render_core_repo}/tests/RenderSkinInputTests.cpp"
    "${_render_core_repo}/src/Render/Scene/ModelSkinInput.cpp")
target_include_directories(war3-render-skin-input-tests PRIVATE "${_render_core_repo}/include")
target_compile_features(war3-render-skin-input-tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(war3-render-skin-input-tests PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(war3-render-skin-input-tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME render-skin-input COMMAND war3-render-skin-input-tests)
set_tests_properties(render-skin-input PROPERTIES TIMEOUT 60)

# 实例名单替换是显式事务，普通姿势更新仍保留固定名单门禁。
add_executable(war3-render-skin-roster-tests
    "${_render_core_repo}/tests/RenderSkinRosterTests.cpp")
target_link_libraries(war3-render-skin-roster-tests PRIVATE War3RenderCore::SkinInput)
war3_render_core_options(war3-render-skin-roster-tests)
add_test(NAME render-skin-roster COMMAND war3-render-skin-roster-tests)
set_tests_properties(render-skin-roster PROPERTIES TIMEOUT 60)

# 静态资产页追加独立于姿势名单替换，保留固定名单更新的拒绝边界。
add_executable(war3-render-skin-asset-append-tests
    "${_render_core_repo}/tests/RenderSkinAssetAppendTests.cpp")
target_link_libraries(war3-render-skin-asset-append-tests PRIVATE War3RenderCore::SkinInput)
war3_render_core_options(war3-render-skin-asset-append-tests)
add_test(NAME render-skin-asset-append COMMAND war3-render-skin-asset-append-tests)
set_tests_properties(render-skin-asset-append PROPERTIES TIMEOUT 60)

# Step4 覆盖修复：独立编译参考，避免全量 Core/Qt 依赖。
add_executable(war3-render-coverage-tests
    "${_render_core_repo}/tests/RenderCoverageTests.cpp"
    "${_render_core_repo}/src/Render/Tools/MdxScan/ScanRecord.cpp"
    "${_render_core_repo}/src/Render/Tools/MdxScan/ScanDetail.cpp"
    "${_render_core_repo}/src/Render/Assets/MdxModel.cpp"
    "${_render_core_repo}/src/Render/Assets/MdxPose.cpp")
target_link_libraries(war3-render-coverage-tests PRIVATE War3RenderCore::CPU)
set_target_properties(war3-render-coverage-tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(war3-render-coverage-tests PRIVATE /W4 /WX /permissive- /utf-8 /fp:precise)
else()
    target_compile_options(war3-render-coverage-tests PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()
add_test(NAME render-coverage-sequences COMMAND war3-render-coverage-tests sequences)
set_tests_properties(render-coverage-sequences PROPERTIES TIMEOUT 60)

add_test(NAME render-coverage-nodes COMMAND war3-render-coverage-tests nodes)
set_tests_properties(render-coverage-nodes PROPERTIES TIMEOUT 60)

add_test(NAME render-coverage-transforms COMMAND war3-render-coverage-tests transforms)
set_tests_properties(render-coverage-transforms PROPERTIES TIMEOUT 60)

# 原来仅手动编译的颜色检查点登记为正式 CPU 回归。
foreach(_color_part IN ITEMS GeosetColor GeosetColorCore)
    add_executable(war3-${_color_part}-tests "${_render_core_repo}/tests/${_color_part}Tests.cpp"
        "${_render_core_repo}/src/Render/Assets/MdxModel.cpp")
    target_link_libraries(war3-${_color_part}-tests PRIVATE War3RenderCore::CPU)
    if(MSVC)
        target_compile_options(war3-${_color_part}-tests PRIVATE /W4 /WX /permissive- /utf-8 /fp:precise)
    else()
        target_compile_options(war3-${_color_part}-tests PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
    endif()
    add_test(NAME render-${_color_part} COMMAND war3-${_color_part}-tests)
    set_tests_properties(render-${_color_part} PROPERTIES TIMEOUT 60)
endforeach()

add_executable(war3-geoset-channels-tests "${_render_core_repo}/tests/GeosetChannelTests.cpp"
    "${_render_core_repo}/tests/RenderCoreAllocProbe.cpp" "${_render_core_repo}/src/Render/Assets/MdxModel.cpp")
target_link_libraries(war3-geoset-channels-tests PRIVATE War3RenderCore::CPU)
if(MSVC)
    target_compile_options(war3-geoset-channels-tests PRIVATE /W4 /WX /permissive- /utf-8 /fp:precise)
else()
    target_compile_options(war3-geoset-channels-tests PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()
add_test(NAME render-geoset-channels COMMAND war3-geoset-channels-tests)
set_tests_properties(render-geoset-channels PROPERTIES TIMEOUT 60)

add_executable(war3-material-channels-tests "${_render_core_repo}/tests/MaterialChannelTests.cpp"
    "${_render_core_repo}/tests/RenderCoreAllocProbe.cpp" "${_render_core_repo}/src/Render/Assets/MdxModel.cpp")
target_link_libraries(war3-material-channels-tests PRIVATE War3RenderCore::CPU)
war3_render_core_options(war3-material-channels-tests)
add_test(NAME render-material-channels COMMAND war3-material-channels-tests)
set_tests_properties(render-material-channels PROPERTIES TIMEOUT 60)

add_executable(war3-material-textures-tests "${_render_core_repo}/tests/MaterialTextureTests.cpp"
    "${_render_core_repo}/tests/RenderCoreAllocProbe.cpp")
target_link_libraries(war3-material-textures-tests PRIVATE War3RenderCore::CPU)
war3_render_core_options(war3-material-textures-tests)
add_test(NAME render-material-textures COMMAND war3-material-textures-tests)
set_tests_properties(render-material-textures PROPERTIES TIMEOUT 60)

# 解析器原测试随唯一上游迁移，保留 FORGE 的原 CTest 名称。
add_executable(war3-map-MdxModel-tests "${_render_core_repo}/tests/MdxModelTests.cpp")
target_link_libraries(war3-map-MdxModel-tests PRIVATE War3RenderCore::CPU)
war3_render_core_options(war3-map-MdxModel-tests)
add_test(NAME render-MdxModel COMMAND war3-map-MdxModel-tests)
set_tests_properties(render-MdxModel PROPERTIES TIMEOUT 60)

if(EXISTS "${_render_core_repo}/tests/ExternalFrameTests.cpp")
    add_executable(war3-render-external-frame-tests
        "${_render_core_repo}/tests/ExternalFrameTests.cpp" "${_render_core_repo}/tests/RenderCoreAllocProbe.cpp")
    target_link_libraries(war3-render-external-frame-tests PRIVATE War3RenderCore::CPU)
    war3_render_core_options(war3-render-external-frame-tests)
    add_test(NAME render-external-frame COMMAND war3-render-external-frame-tests)
    set_tests_properties(render-external-frame PROPERTIES TIMEOUT 60)
endif()
