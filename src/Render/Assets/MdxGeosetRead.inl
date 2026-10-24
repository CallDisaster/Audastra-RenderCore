// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

// 在 MdxModel.cpp 匿名命名空间内包含；每个 GEOA 对象拥有独立有界 reader。
std::vector<GeosetAnimation> ParseGeosetAnimations(Reader reader, const MdxLimits& limits, bool staticBudget = false) {
    std::vector<GeosetAnimation> animations;
    if (staticBudget) animations.reserve(reader.Remaining() / 28);
    while (reader.Remaining() != 0) {
        const auto start = reader.Position(), bytes = static_cast<std::size_t>(reader.U32("geoset 动画大小"));
        if (bytes < 28 || bytes - 4 > reader.Remaining()) Fail("MDX_SIZE_INVALID", "geoset 动画大小无效。");
        if (animations.size() >= limits.maxGeosets) Fail("MDX_LIMIT", "geoset 动画数量超过上限。");
        auto body = reader.Until(start + bytes, "GEOA");
        GeosetAnimation animation;
        auto& a = animation.value;
        a.staticAlpha = body.F32("geoset 透明度"); a.flags = body.U32("geoset 动画标志");
        // 1.27a 静态字段最终按文件 RGB 消费；KGAC 值和切线仍为 BGR。
        for (std::size_t c = 0; c < 3; ++c) a.staticColor[c] = std::bit_cast<float>(body.U32("geoset RGB"));
        animation.geosetId = body.U32("geoset ID");
        while (body.Remaining() >= 16) {
            const auto tag = body.PeekTag();
            if (tag != "KGAO" && tag != "KGAC") break;
            body.Tag("GEOA 轨迹");
            const auto count = body.U32("按键数"), mode = body.U32("插值"), global = body.U32("全局序列");
            if (count > limits.maxTrackKeys) Fail("MDX_LIMIT", "GEOA 按键超过上限。");
            if (mode > 3) Fail("MDX_GEOA_INTERPOLATION_UNSUPPORTED", "未知 GEOA 插值，不能猜测按键大小。");
            const bool color = tag == "KGAC", tangents = mode >= 2;
            body.Need(static_cast<std::size_t>(count) * (4 + (color ? 12u : 4u) * (tangents ? 3u : 1u)), "GEOA 按键");
            if (color ? a.hasColorTrack : a.hasAlphaTrack) {
                a.hasUninterpretedData = true;
                body.Skip(static_cast<std::size_t>(count) * (4 + (color ? 12u : 4u) * (tangents ? 3u : 1u)), "重复GEOA轨迹");
                continue;
            }
            if (color) {
                a.hasColorTrack = true; a.colorInterpolation = mode; a.colorGlobalSequence = global;
                a.colorKeys.reserve(count);
            } else {
                a.hasAlphaTrack = true; a.interpolation = mode; a.globalSequence = global;
                a.alphaKeys.reserve(count);
            }
            for (std::uint32_t k = 0; k < count; ++k) {
                if (color) {
                    MdxGeosetColorKey key; key.frame = body.U32("KGAC 帧");
                    const auto bgr = [&](auto& value) { for (std::size_t c = 0; c < 3; ++c)
                        value[2-c] = std::bit_cast<float>(body.U32("KGAC BGR")); };
                    bgr(key.value);
                    if (tangents) { bgr(key.inTangent); bgr(key.outTangent); }
                    a.colorKeys.push_back(key);
                } else {
                    MdxGeosetAlphaKey key; key.frame = body.U32("KGAO 帧"); key.value = body.F32("KGAO 值");
                    if (tangents) { key.inTangent = body.F32("KGAO 入切线"); key.outTangent = body.F32("KGAO 出切线"); }
                    a.alphaKeys.push_back(key);
                }
            }
        }
        if (body.Remaining() != 0) a.hasUninterpretedData = true;
        reader.Skip(bytes - 4, "GEOA 对象"); animations.push_back(std::move(animation));
    }
    return animations;
}
