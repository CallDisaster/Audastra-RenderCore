// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

// 冻结节点参考之外的 GEOA 通用参考：独立扫描原始按键，不调用 RenderCore。
namespace {
float GeoComponent(float v, std::size_t) { return v; }
float GeoComponent(const std::array<float,3>& v, std::size_t c) { return v[c]; }
template<std::size_t N, class Keys>
Result<std::array<float,N>> SampleGeoReference(const MdxModel& model, const MdxGeosetAlphaRequest& request,
    std::array<float,N> fallback, bool hasTrack, std::uint32_t mode, std::uint32_t global, const Keys& keys) {
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
    if (global != UINT32_MAX) {
        if (global >= model.globalSequences.size())
            return reject("MDX_GEOA_GLOBAL_SEQUENCE_UNSUPPORTED");
        start = 0; end = model.globalSequences[global]; time = 0;
        if (end != 0) time = request.globalElapsedMs % end;
        if (!keys.empty()) time += keys.front().frame; // 原始坐标上采样，不共享编译侧键表。
    }
    if (keys.size() > MdxLimits{}.maxTrackKeys) return reject("MDX_GEOA_LIMIT");
    const bool singleGlobal = global != UINT32_MAX && keys.size() == 1;
    using Key = typename Keys::value_type;
    const Key* left = nullptr; const Key* right = nullptr;
    bool first = true, hasKeys = false; std::uint32_t previous = 0;
    for (const auto& key : keys) {
        // 保留先数值、后帧序校验次序；编译侧采用同一错误优先级，不共用结果。
        for (std::size_t c=0;c<N;++c) {
            const auto v=GeoComponent(key.value,c);
            if (!std::isfinite(v) || (rgb && (v<0 || v>1)) ||
                (mode>=2 && (!std::isfinite(GeoComponent(key.inTangent,c)) || !std::isfinite(GeoComponent(key.outTangent,c)))))
                return reject(number);
        }
        if (!first && key.frame<=previous) return reject("MDX_GEOA_KEYS_INVALID");
        first=false; previous=key.frame;
        if (global != UINT32_MAX && key.frame-keys.front().frame>INT32_MAX)
            return reject("MDX_GEOA_GLOBAL_SPAN_UNSUPPORTED");
        if (singleGlobal) { left=&key;hasKeys=true;continue; }
        if (global == UINT32_MAX && (key.frame<start || key.frame>end)) continue;
        hasKeys=true;
        if (key.frame<=time) left=&key; else if (!right) right=&key;
    }
    if (!hasKeys) return Out::Success(fallback);
    if (!left) return reject("MDX_GEOA_BOUNDARY_UNSUPPORTED");
    std::array<float,N> result{};
    if (singleGlobal || mode==0 || left->frame==time) {
        for (std::size_t c=0;c<N;++c) result[c]=GeoComponent(left->value,c);
        return Out::Success(result);
    }
    if (!right) return reject("MDX_GEOA_BOUNDARY_UNSUPPORTED");
    const auto t=static_cast<double>(time-left->frame)/(static_cast<double>(right->frame)-left->frame), u=1.0-t;
    for (std::size_t c=0;c<N;++c) {
        const double a=GeoComponent(left->value,c), b=GeoComponent(right->value,c);
        const double out=GeoComponent(left->outTangent,c), in=GeoComponent(right->inTangent,c);
        double value=0;
        if (mode==1) value=u*a+t*b;
        else if (mode==2) value=(2*t*t*t-3*t*t+1)*a+(t*t*t-2*t*t+t)*out+(-2*t*t*t+3*t*t)*b+(t*t*t-t*t)*in;
        else value=u*u*u*a+3*u*u*t*out+3*u*t*t*in+t*t*t*b;
        result[c]=static_cast<float>(value);
        if (!std::isfinite(result[c])) return reject(number);
        if constexpr (rgb) result[c]=std::clamp(result[c],0.0f,1.0f); // 显式归一化 RGB 预览规则，不模拟原版量化。
    }
    return Out::Success(result);
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
