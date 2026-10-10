// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "GeosetChannels.h"
#include "War3RenderCore/GameKeyRanges.h"
#include <algorithm>
#include <bit>
#include <cmath>
namespace War3RenderCore::Detail {
namespace {
float Component(float v,std::size_t) { return v; }
float Component(const Vec3& v,std::size_t c) { return v[c]; }
// sequences 非空（GEOA）时，非全局轨迹按原版 1.27a 规则编译：KGAO/KGAC 编译（6F780270/6F780470）同样调用
// 6F78A090 切逐动作区间，不排序源键；取值走 6F789EB0 顺序查找与 6F77A720/6F78A1B0 的接缝插值。
// 材质透明度暂不传 sequences，保持原严格递增合同。
template<std::size_t N,class Keys>
GeoChannel<N> CompileChannel(std::array<float,N> fallback,bool hasTrack,std::uint32_t mode,
    std::uint32_t global,const Keys& keys,const std::vector<std::uint32_t>& periods,
    bool identicalDuplicatesPreview = false,const std::vector<War3Map::Render::MdxSequence>* sequences = nullptr) {
    GeoChannel<N> out; out.fallback=fallback; out.mode=mode;
    constexpr bool rgb=N==3;
    const auto number=rgb ? Code::GeoaColorInvalid : Code::GeoaNumberInvalid;
    for (auto v:fallback) if (!std::isfinite(v) || (rgb && (v<0 || v>1))) { out.error=number;return out; }
    if (!hasTrack) return out;
    if (mode>3) { out.error=Code::GeoaInterpolationUnsupported;return out; }
    out.global=global!=NoIndex;
    if (out.global) {
        if (global>=periods.size()) { out.error=Code::GeoaGlobalSequenceUnsupported;return out; }
        out.period=periods[global];
    }
    if (keys.size()>War3Map::Render::MdxLimits{}.maxTrackKeys) { out.error=Code::GeoaLimit;return out; }
    out.keys.reserve(keys.size());
    const auto origin=out.global && !keys.empty() ? keys.front().frame : 0u;
    std::uint32_t previous=0; bool first=true;
    for (const auto& source:keys) {
        GeoKey<N> key; key.frame=source.frame;
        for (std::size_t c=0;c<N;++c) {
            key.value[c]=Component(source.value,c); key.inTangent[c]=Component(source.inTangent,c); key.outTangent[c]=Component(source.outTangent,c);
            const float v=key.value[c];
            if (!std::isfinite(v) || (rgb && (v<0 || v>1)) ||
                (mode>=2 && (!std::isfinite(key.inTangent[c]) || !std::isfinite(key.outTangent[c])))) {
                out.error=number;out.keys.clear();return out;
            }
        }
        const bool gameRanges=sequences && !out.global;
        if (!gameRanges && !first && key.frame<=previous) {
            bool identical = identicalDuplicatesPreview && !out.global && key.frame==previous;
            if (identical) for (std::size_t c=0;c<N;++c) {
                const auto& prior=out.keys.back();
                identical &= std::bit_cast<std::uint32_t>(key.value[c])==std::bit_cast<std::uint32_t>(prior.value[c]);
                if (mode>=2) identical &= std::bit_cast<std::uint32_t>(key.inTangent[c])==std::bit_cast<std::uint32_t>(prior.inTangent[c])
                    && std::bit_cast<std::uint32_t>(key.outTangent[c])==std::bit_cast<std::uint32_t>(prior.outTangent[c]);
            }
            if (!identical) { out.error=Code::GeoaKeysInvalid;out.keys.clear();return out; }
        }
        first=false;previous=key.frame;
        key.frame-=origin;
        if (out.global && key.frame>INT32_MAX) { out.error=Code::GeoaGlobalSpanUnsupported;out.keys.clear();return out; }
        out.keys.push_back(key);
    }
    if (sequences && !out.global) {
        out.ranges.reserve(sequences->size()); out.descending.reserve(sequences->size());
        ForEachGameSequenceRange(out.keys,*sequences,[&](std::size_t,std::size_t offset,std::size_t count) {
            std::int64_t left=0,right=0;
            out.ranges.push_back({static_cast<std::uint32_t>(offset),static_cast<std::uint32_t>(count)});
            out.descending.push_back(FirstDescendingEdge(out.keys,offset,count,left,right) ? 1 : 0);
            return true;
        });
    }
    return out;
}
template<std::size_t N>
std::array<float,N> Interpolate(const GeoKey<N>& left,const GeoKey<N>& right,double t,std::uint32_t mode,Evaluation& e) noexcept {
    const double u=1.0-t;
    std::array<float,N> value{};
    for (std::size_t c=0;c<N;++c) {
        const double a=left.value[c],b=right.value[c],out=left.outTangent[c],in=right.inTangent[c];
        double result=0;
        if (mode==1) result=u*a+t*b;
        else if (mode==2) result=(2*t*t*t-3*t*t+1)*a+(t*t*t-2*t*t+t)*out+(-2*t*t*t+3*t*t)*b+(t*t*t-t*t)*in;
        else result=u*u*u*a+3*u*u*t*out+3*u*t*t*in+t*t*t*b;
        value[c]=static_cast<float>(result);
        if (!std::isfinite(value[c])) { e.Fail(N==3 ? Code::GeoaColorInvalid : Code::GeoaNumberInvalid);return {}; }
        if constexpr(N==3) value[c]=std::clamp(value[c],0.0f,1.0f);
    }
    return value;
}
template<std::size_t N>
std::array<float,N> Sample(const GeoChannel<N>& channel,const Sequence& sequence,std::uint32_t sequenceIndex,
    std::uint32_t frame,std::uint64_t global,Evaluation& e,bool identicalDuplicatesPreview = false) noexcept {
    if (channel.error!=Code::Ok) { e.Fail(channel.error);return {}; }
    if (!channel.global && !channel.ranges.empty()) {
        if (sequenceIndex>=channel.ranges.size()) { e.Fail(Code::PoseSequenceInvalid);return {}; }
        const auto range=channel.ranges[sequenceIndex];
        const bool descending=channel.descending[sequenceIndex]!=0;
        // 区间含逆序边：严格模式拒绝；显式原序预览（与节点同一策略）按原版顺序查找。
        if (descending && e.nodeKeys!=NodeKeySamplingPolicy::SourceOrderPreview) { e.Fail(Code::GeoaKeysInvalid);return {}; }
        const auto window=SelectGameKeyWindow(channel.keys,range.offset,range.count,
            sequence.start,sequence.end,frame,!descending);
        if (window.kind==SourceOrderWindowKind::Empty) return channel.fallback;
        const auto& left=channel.keys[window.left];
        // 6F77A940：区间单键直接取值；6F77A720：跨度为 0 或无插值取左键；首键前比例为负（原版写法）。
        if (window.kind==SourceOrderWindowKind::Value || channel.mode==0 || window.span==0) return left.value;
        return Interpolate(left,channel.keys[window.right],
            static_cast<double>(window.offset)/static_cast<double>(window.span),channel.mode,e);
    }
    if (channel.global && channel.keys.size()==1) return channel.keys.front().value;
    const auto start=channel.global ? 0u : sequence.start, end=channel.global ? channel.period : sequence.end;
    const auto time=channel.global ? (channel.period ? static_cast<std::uint32_t>(global%channel.period) : 0u) : frame;
    const auto begin=channel.global ? channel.keys.begin() : std::lower_bound(channel.keys.begin(),channel.keys.end(),start,[](const auto& k,auto t) { return k.frame<t; });
    const auto stop=channel.global ? channel.keys.end() : std::upper_bound(begin,channel.keys.end(),end,[](auto t,const auto& k) { return t<k.frame; });
    if (begin==stop) return channel.fallback;
    // 1.27a 789EB0/77AA80/77AAA0：当前动作只有一键时直接取值，即使尚未到该帧。
    if (!channel.global && (stop-begin==1 || (identicalDuplicatesPreview && begin->frame==(stop-1)->frame)))
        return begin->value; // 合资格的普通同帧同值组等价于单时点，原键仍全数拥有。
    const auto right=std::upper_bound(begin,stop,time,[](auto t,const auto& k) { return t<k.frame; });
    if (right==begin) { e.Fail(Code::GeoaBoundaryUnsupported);return {}; }
    const auto& left=*(right-1);
    if (channel.mode==0 || left.frame==time) return left.value;
    if (right==stop) { e.Fail(Code::GeoaBoundaryUnsupported);return {}; }
    return Interpolate(left,*right,static_cast<double>(time-left.frame)/(static_cast<double>(right->frame)-left.frame),channel.mode,e);
}
}
Alpha CompileMaterialScalarAlpha(const War3Map::Render::MdxLayer& source,
    const std::vector<std::uint32_t>& periods, bool identicalDuplicatesPreview) {
    return CompileChannel<1>({source.alpha}, source.animatedAlpha, source.alphaInterpolation,
        source.alphaGlobalSequence, source.alphaKeys, periods, identicalDuplicatesPreview);
}
float SampleScalarAlpha(const Alpha& channel, const Sequence& sequence, std::uint32_t frame,
    std::uint64_t global, Evaluation& e, bool identicalDuplicatesPreview) noexcept {
    return Sample(channel, sequence, 0, frame, global, e, identicalDuplicatesPreview)[0];
}
Alpha CompileGeosetAlpha(const std::optional<War3Map::Render::MdxGeosetAnimation>& source,const std::vector<std::uint32_t>& periods,
    const std::vector<War3Map::Render::MdxSequence>& sequences) {
    Alpha out;out.fallback={1};
    if (!source) return out;
    const auto& a=*source;
    if ((a.flags&~2u)!=0 || a.hasUninterpretedData) { out.error=Code::GeoaUninterpreted;return out; }
    return CompileChannel<1>({a.staticAlpha},a.hasAlphaTrack,a.interpolation,a.globalSequence,a.alphaKeys,periods,false,&sequences);
}
ColorChannel CompileGeosetRgb(const std::optional<War3Map::Render::MdxGeosetAnimation>& source,const std::vector<std::uint32_t>& periods,
    const std::vector<War3Map::Render::MdxSequence>& sequences) {
    ColorChannel out;out.fallback={1,1,1};
    if (!source) return out;
    const auto& a=*source;
    if ((a.flags&~2u)!=0 || a.hasUninterpretedData) { out.error=Code::GeoaUninterpreted;return out; }
    if (!(a.flags&2u)) return out;
    return CompileChannel<3>(a.staticColor,a.hasColorTrack,a.colorInterpolation,a.colorGlobalSequence,a.colorKeys,periods,false,&sequences);
}
bool EvaluateGeosets(const ModelData& model,std::uint32_t sequenceIndex,std::uint32_t frame,std::uint64_t global,
    std::span<float> alphas,std::span<Vec3> colors,Evaluation& e) noexcept {
    const auto& sequence=model.sequences[sequenceIndex];
    for (std::size_t i=0;i<model.alphas.size();++i) {
        alphas[i]=Sample(model.alphas[i],sequence,sequenceIndex,frame,global,e)[0];
        if (!e.Good()) return false;
        colors[i]=Sample(model.colorTracks[i],sequence,sequenceIndex,frame,global,e);
        if (!e.Good()) return false;
    }
    return true;
}
bool EvaluateGeosets(const ModelData& model,std::uint32_t sequenceIndex,std::uint32_t frame,std::uint64_t global,
    std::span<float> alphas,std::span<Vec3> colors,Evaluation& e,
    std::span<GeosetIssue> issues,std::uint32_t& issueCount) noexcept {
    const auto& sequence=model.sequences[sequenceIndex];
    issueCount=0;
    if (alphas.size()!=model.alphas.size() || colors.size()!=model.colorTracks.size() ||
        model.alphas.size()!=model.colorTracks.size()) { e.Fail(Code::OutputRangeInvalid);return false; }
    if (issues.size()<model.alphas.size()) { e.Fail(Code::CapacityExceeded);return false; }
    for (std::size_t i=0;i<model.alphas.size();++i) {
        Evaluation local{};
        local.nodeKeys=e.nodeKeys;
        alphas[i]=Sample(model.alphas[i],sequence,sequenceIndex,frame,global,local)[0];
        if (local.Good()) colors[i]=Sample(model.colorTracks[i],sequence,sequenceIndex,frame,global,local);
        if (local.Good()) continue;
        // 只隔离已界定 GEOA 的四类内容；结构、引用、节点及资源预算不进入此白名单。
        if (local.error!=Code::GeoaUninterpreted && local.error!=Code::GeoaColorInvalid &&
            local.error!=Code::GeoaKeysInvalid && local.error!=Code::GeoaBoundaryUnsupported) {
            e.Fail(local.error);return false;
        }
        issues[issueCount++]={static_cast<std::uint32_t>(i),{local.error,Severity::Error,NoIndex,TrackKind::None,0,0}};
        alphas[i]=0; colors[i]={0,0,0}; // 明确不显示；不是未知颜色的成功值。
    }
    return true;
}
}
