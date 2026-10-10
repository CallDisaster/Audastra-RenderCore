// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

// 冻结节点参考之外的 GEOA 通用参考：独立扫描原始按键，不调用 RenderCore 编译结果。
// 非全局轨迹按原版 1.27a 规则：逐动作区间（6F78A090）+ 区间内顺序线性查找与接缝（6F789EB0/6F77A720/6F78A1B0），
// 规则本身取共享头 GameKeyRanges.h，查找固定走线性扫描（核心对单调区间用二分，两者互为校验）。
namespace {
float GeoComponent(float v, std::size_t) { return v; }
float GeoComponent(const std::array<float,3>& v, std::size_t c) { return v[c]; }
template<std::size_t N, class Key>
std::optional<std::array<float,N>> GeoInterpolate(const Key& left, const Key& right, double t, std::uint32_t mode) {
    constexpr bool rgb = N == 3;
    const double u = 1.0 - t;
    std::array<float,N> result{};
    for (std::size_t c=0;c<N;++c) {
        const double a=GeoComponent(left.value,c), b=GeoComponent(right.value,c);
        const double out=GeoComponent(left.outTangent,c), in=GeoComponent(right.inTangent,c);
        double value=0;
        if (mode==1) value=u*a+t*b;
        else if (mode==2) value=(2*t*t*t-3*t*t+1)*a+(t*t*t-2*t*t+t)*out+(-2*t*t*t+3*t*t)*b+(t*t*t-t*t)*in;
        else value=u*u*u*a+3*u*u*t*out+3*u*t*t*in+t*t*t*b;
        result[c]=static_cast<float>(value);
        if (!std::isfinite(result[c])) return std::nullopt;
        if constexpr (rgb) result[c]=std::clamp(result[c],0.0f,1.0f); // 显式归一化 RGB 预览规则，不模拟原版量化。
    }
    return result;
}
template<std::size_t N, class Keys>
Result<std::array<float,N>> SampleGeoReference(const MdxModel& model, const MdxGeosetAlphaRequest& request,
    std::array<float,N> fallback, bool hasTrack, std::uint32_t mode, std::uint32_t global, const Keys& keys,
    bool gameRanges = true) {
    using Out = Result<std::array<float,N>>;
    const auto reject = [](const char* code) { return Out::Failure("Mdx.Geoset", code,"GEOA 轨迹不满足有界预览合同。"); };
    constexpr bool rgb = N == 3;
    const char* number = rgb ? "MDX_GEOA_COLOR_INVALID" : "MDX_GEOA_NUMBER_INVALID";
    for (float v : fallback) if (!std::isfinite(v) || (rgb && (v < 0 || v > 1))) return reject(number);
    if (!hasTrack) return Out::Success(fallback);
    if (mode > 3) return reject("MDX_GEOA_INTERPOLATION_UNSUPPORTED");
    const auto& sequence = model.sequences[request.sequenceIndex];
    std::uint64_t start = sequence.start, end = sequence.end;
    const auto duration = end-start;
    std::uint64_t time = start + (sequence.flags == 0 && duration ? request.sequenceElapsedMs % duration
        : std::min(request.sequenceElapsedMs,duration));
    const bool isGlobal = global != UINT32_MAX;
    if (isGlobal) {
        if (global >= model.globalSequences.size())
            return reject("MDX_GEOA_GLOBAL_SEQUENCE_UNSUPPORTED");
        start = 0; end = model.globalSequences[global]; time = 0;
        if (end != 0) time = request.globalElapsedMs % end;
        if (!keys.empty()) time += keys.front().frame; // 原始坐标上采样，不共享编译侧键表。
    }
    if (keys.size() > MdxLimits{}.maxTrackKeys) return reject("MDX_GEOA_LIMIT");
    bool first = true; std::uint32_t previous = 0;
    for (const auto& key : keys) {
        // 保留先数值、后帧序校验次序；编译侧采用同一错误优先级，不共用结果。
        for (std::size_t c=0;c<N;++c) {
            const auto v=GeoComponent(key.value,c);
            if (!std::isfinite(v) || (rgb && (v<0 || v>1)) ||
                (mode>=2 && (!std::isfinite(GeoComponent(key.inTangent,c)) || !std::isfinite(GeoComponent(key.outTangent,c)))))
                return reject(number);
        }
        // 全局轨迹（6F77B1E0 全键查找）与材质透明度仍须严格递增；GEOA 非全局键按原版不排序、不拒绝。
        if ((isGlobal || !gameRanges) && !first && key.frame<=previous) return reject("MDX_GEOA_KEYS_INVALID");
        first=false; previous=key.frame;
        if (isGlobal && key.frame-keys.front().frame>INT32_MAX)
            return reject("MDX_GEOA_GLOBAL_SPAN_UNSUPPORTED");
    }
    std::array<float,N> value{};
    const auto constant = [&](const auto& key) {
        for (std::size_t c=0;c<N;++c) value[c]=GeoComponent(key.value,c);
        return Out::Success(value);
    };
    const auto curve = [&](const auto& left, const auto& right, double t) {
        const auto sampled = GeoInterpolate<N>(left, right, t, mode);
        return sampled ? Out::Success(*sampled) : reject(number);
    };
    if (!isGlobal && gameRanges) {
        // 原版逐动作区间 + 区间内线性顺序查找；单键直接取值，首键前比例为负，跨度为负加动作长度。
        std::size_t offset = 0, count = 0;
        War3RenderCore::ForEachGameSequenceRange(keys, model.sequences, [&](std::size_t index, std::size_t begin, std::size_t size) {
            if (index != request.sequenceIndex) return true;
            offset = begin; count = size; return false;
        });
        const auto window = War3RenderCore::SelectGameKeyWindow(keys, offset, count, static_cast<std::int64_t>(start),
            static_cast<std::int64_t>(end), static_cast<std::int64_t>(time));
        if (window.kind == War3RenderCore::SourceOrderWindowKind::Empty) return Out::Success(fallback);
        if (window.kind == War3RenderCore::SourceOrderWindowKind::Value || mode == 0 || window.span == 0)
            return constant(keys[window.left]);
        return curve(keys[window.left], keys[window.right],
            static_cast<double>(window.offset) / static_cast<double>(window.span));
    }
    // 全局轨迹与材质透明度：严格递增键按时间扫描；材质只取当前动作 [start,end] 内的键。
    if (isGlobal && keys.size() == 1) return constant(keys.front());
    using Key = typename Keys::value_type;
    const Key* left = nullptr; const Key* right = nullptr; const Key* soleKey = nullptr;
    std::size_t spanCount = 0;
    for (const auto& key : keys) {
        if (!isGlobal && (key.frame<start || key.frame>end)) continue;
        soleKey=&key; ++spanCount;
        if (key.frame<=time) left=&key; else if (!right) right=&key;
    }
    if (!spanCount) return Out::Success(fallback);
    if (!isGlobal && spanCount == 1) return constant(*soleKey); // 普通单键与原版未抑制更新路径一样为常量
    if (!left) return reject("MDX_GEOA_BOUNDARY_UNSUPPORTED");
    if (mode==0 || left->frame==time) return constant(*left);
    if (!right) return reject("MDX_GEOA_BOUNDARY_UNSUPPORTED");
    return curve(*left, *right, static_cast<double>(time-left->frame)/(static_cast<double>(right->frame)-left->frame));
}
}
Result<float> SampleMdxGeosetAlpha(const MdxModel& model, const MdxGeosetAlphaRequest& request) {
    const auto fail=[](const char* code) { return Result<float>::Failure("Mdx.GeosetAlpha",code,"GEOA 透明度无法确定。"); };
    if (request.geosetIndex>=model.geosets.size() || request.sequenceIndex>=model.sequences.size()) return fail("MDX_GEOA_REFERENCE_INVALID");
    if (!model.sequences[request.sequenceIndex].Usable()) return fail("MDX_GEOA_SEQUENCE_UNSUPPORTED");
    const auto& optional=model.geosets[request.geosetIndex].animation;
    if (!optional) return Result<float>::Success(1);
    const auto& a=*optional;
    if ((a.flags&~2u)!=0 || a.hasUninterpretedData) return fail("MDX_GEOA_UNINTERPRETED");
    auto sampled=SampleGeoReference<1>(model,request,{a.staticAlpha},a.hasAlphaTrack,a.interpolation,a.globalSequence,a.alphaKeys);
    if (!sampled.Succeeded()) return {std::nullopt,std::move(sampled.diagnostics)};
    return Result<float>::Success((*sampled.value)[0]);
}
Result<std::array<float,3>> SampleMdxGeosetColor(const MdxModel& model,const MdxGeosetAlphaRequest& request) {
    using Out=Result<std::array<float,3>>;
    const auto fail=[](const char* code) { return Out::Failure("Mdx.GeosetColor",code,"GEOA 颜色无法确定。"); };
    if (request.geosetIndex>=model.geosets.size() || request.sequenceIndex>=model.sequences.size()) return fail("MDX_GEOA_REFERENCE_INVALID");
    if (!model.sequences[request.sequenceIndex].Usable()) return fail("MDX_GEOA_SEQUENCE_UNSUPPORTED");
    const auto& optional=model.geosets[request.geosetIndex].animation;
    if (!optional) return Out::Success({1,1,1});
    const auto& a=*optional;
    if ((a.flags&~2u)!=0 || a.hasUninterpretedData) return fail("MDX_GEOA_UNINTERPRETED");
    if (!(a.flags&2u)) return Out::Success({1,1,1});
    return SampleGeoReference<3>(model,request,a.staticColor,a.hasColorTrack,a.colorInterpolation,a.colorGlobalSequence,a.colorKeys);
}
