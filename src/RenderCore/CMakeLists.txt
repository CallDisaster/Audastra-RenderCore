# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 CallDisaster


add_library(war3-render-core STATIC
    CompiledModel.cpp GeosetChannels.cpp MaterialChannels.cpp MaterialTextures.cpp Diagnostics.cpp PoseMath.cpp PoseInheritance.cpp PoseTracks.cpp PoseBatch.cpp PreviewSkin.cpp)
add_library(War3RenderCore::CPU ALIAS war3-render-core)
target_link_libraries(war3-render-core PUBLIC War3RenderCore::MDX)
war3_render_core_options(war3-render-core)
