// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "GeosetChannelFixture.h"
#include "War3MapCore/Render/MdxModel.h"
#include "War3RenderCore/PoseBatch.h"
#include "RenderCoreAllocProbe.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace War3Map::Render;
using namespace War3RenderCore;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
using GeosetChannelFixture::Bytes;

Code Compare(const MdxModel& model,std::uint64_t time,std::uint64_t global) {
    const auto built=CompileMdxModel(model,{1,1},{"channels.mdx","synthetic","rgb"});
    Check(built.Succeeded(),"通道编译失败");
    const std::array assets{*built.value}; auto made=PoseBatchEvaluator::Create(assets,1);
    Check(made.Succeeded(),"通道预热失败");auto evaluator=std::move(*made.value);
    PoseRequest request;request.asset={1,1};request.instance={1,1};request.sequenceIndex=1;
    request.sequenceElapsedMs=time;request.globalElapsedMs=global;
    RenderCoreAllocProbe::Begin();auto result=evaluator.Evaluate(std::span(&request,1));const auto allocated=RenderCoreAllocProbe::End();
    Check(result.error==Code::Ok && allocated==0,"颜色热路径分配或批次失败");
    std::string error;
    for (std::uint32_t g=0;g<model.geosets.size();++g) {
        const auto a=SampleMdxGeosetAlpha(model,{g,1,time,global});
        if (!a.Succeeded()) { error=a.diagnostics[0].code;break; }
        const auto c=SampleMdxGeosetColor(model,{g,1,time,global});
        if (!c.Succeeded()) { error=c.diagnostics[0].code;break; }
        if (result.failedInstances==0) {
            Check(std::memcmp(&*a.value,&evaluator.Alphas(0)[g],4)==0,"透明度新旧逐位差分");
            Check(std::memcmp(c.value->data(),evaluator.Colors(0)[g].data(),12)==0,"RGB 新旧逐位差分");
        }
    }
    const auto code=evaluator.Receipts()[0].error;
    Check(error==(code==Code::Ok ? "" : CodeName(code)),"通道拒绝码差分");
    if (code!=Code::Ok) Check(evaluator.Colors(0).empty() && evaluator.Alphas(0).empty(),"失败泄漏部分颜色");
    return code;
}
void Run() {
    std::uint32_t samples=0;
    for (std::uint32_t mode=0;mode<4;++mode) for (const bool global:{false,true}) {
        const auto bytes=Bytes(mode,global);const auto parsed=ParseMdx(bytes);Check(parsed.Succeeded(),"真实格式 KGAC 解析失败");
        const auto& a=*parsed.value->geosets[1].animation;
        Check(a.hasColorTrack && a.colorInterpolation==mode && a.colorKeys[0].value==Vec3{0,0.25f,1},"KGAC BGR/插值丢失");
        for (auto t:{0u,1u,123u,250u,500u,999u,1000u,1521u}) {
            Check(Compare(*parsed.value,t,t)==Code::Ok,"完整按键区间被拒绝");++samples;
        }
        // 独立手算中点；不能只让两个实现复制同一个错误。
        const auto alpha=SampleMdxGeosetAlpha(*parsed.value,{1,1,500,500});
        const auto color=SampleMdxGeosetColor(*parsed.value,{1,1,500,500});
        Check(alpha.Succeeded() && color.Succeeded(),"手算样本失败");
        Check(*alpha.value==(mode==0 ? 0.0f : 0.5f),"KGAO 多项式中点错误");
        Check((*color.value)[0]==(mode==0 ? 0.0f : 0.5f) && (*color.value)[2]==(mode==0 ? 1.0f : 0.5f),"KGAC 多项式/BGR 错误");
        auto truncated=bytes;truncated.pop_back();Check(!ParseMdx(truncated).Succeeded(),"截断未拒绝");
    }
    auto zero=*ParseMdx(Bytes(1,true)).value;zero.globalSequences[0]=0;
    Check(Compare(zero,500,UINT64_MAX)==Code::Ok,"零周期 GEOA 差分");
    Check(*SampleMdxGeosetAlpha(zero,{1,1,500,UINT64_MAX}).value==0 &&
        *SampleMdxGeosetColor(zero,{1,1,500,UINT64_MAX}).value==Vec3{0,0.25f,1},"零周期 GEOA 被混为动作时钟");
    auto& constant=*zero.geosets[1].animation;constant.alphaKeys.resize(1);constant.colorKeys.resize(1);
    constant.alphaKeys[0].frame=constant.colorKeys[0].frame=333;constant.alphaKeys[0].value=0.25f;
    for (auto period:{0u,100u}) { zero.globalSequences[0]=period;Check(Compare(zero,500,UINT64_MAX)==Code::Ok,"全局单键 GEOA 差分");
        Check(*SampleMdxGeosetAlpha(zero,{1,1,500,UINT64_MAX}).value==0.25f &&
            *SampleMdxGeosetColor(zero,{1,1,500,UINT64_MAX}).value==Vec3{0,0.25f,1},"全局单键 GEOA 误取默认值"); }
    zero.globalSequences[0]=0;constant.alphaKeys.push_back(constant.alphaKeys[0]);constant.alphaKeys.back().frame=500;
    Check(Compare(zero,500,0)==Code::Ok && *SampleMdxGeosetAlpha(zero,{1,1,500,0}).value==0.25f,"零周期KGAO未取归零首键");
    constant.colorKeys.push_back(constant.colorKeys[0]);constant.colorKeys.back().frame=500;
    Check(Compare(zero,500,0)==Code::Ok && *SampleMdxGeosetColor(zero,{1,1,500,0}).value==Vec3{0,0.25f,1},"零周期KGAC未取归零首键");
    zero.globalSequences[0]=100;constant.alphaKeys.back().frame=constant.colorKeys.back().frame=533;
    constant.alphaKeys.back().value=0.75f;constant.colorKeys.back().value={1,0.25f,0};
    Check(Compare(zero,500,50)==Code::Ok && *SampleMdxGeosetAlpha(zero,{1,1,500,50}).value==0.375f &&
        *SampleMdxGeosetColor(zero,{1,1,500,50}).value==Vec3{0.25f,0.25f,0.75f},"归零后周期外右键插值错误");
    constant.colorKeys.back().frame=UINT32_MAX;Check(Compare(zero,500,0)==Code::GeoaGlobalSpanUnsupported,"全局帧跨度溢出未拒绝");
    auto model=*ParseMdx(Bytes(2,false)).value;
    model.geosets[1].animation->colorKeys[0].outTangent[0]=20;
    Check(Compare(model,500,0)==Code::Ok,"有限颜色曲线不能产生 NaN");
    Check((*SampleMdxGeosetColor(model,{1,1,500,0}).value)[0]==1,"RGB 预览夹取没有确定值");
    auto bad=model;bad.geosets[1].animation->colorInterpolation=99;Check(Compare(bad,500,0)==Code::GeoaInterpolationUnsupported,"未知插值未拒绝");
    bad=model;bad.geosets[1].animation->colorKeys[0].outTangent[0]=std::numeric_limits<float>::quiet_NaN();
    Check(Compare(bad,500,0)==Code::GeoaColorInvalid,"NaN 控制点未拒绝");
    bad=model;bad.geosets[1].animation->colorGlobalSequence=0;
    Check(Compare(bad,500,0)==Code::GeoaGlobalSequenceUnsupported,"无效全局序列未拒绝");
    bad=model;bad.geosets[1].animation->colorKeys[0].frame=100;
    Check(Compare(bad,50,0)==Code::GeoaBoundaryUnsupported,"首键前未拒绝");
    bad=model;bad.geosets[1].animation->alphaKeys[1].frame=700;
    Check(Compare(bad,800,0)==Code::GeoaBoundaryUnsupported,"曲线尾键后未拒绝");
    bad=model;bad.geosets[1].animation->flags=1;
    Check(Compare(bad,500,0)==Code::GeoaUninterpreted,"未知投影位未拒绝");
    // 两实例 RGB 在同一批独立，重复求值逐位一致，外部组矩阵仍使用各自时钟颜色。
    auto plain=*ParseMdx(Bytes(1,false)).value;auto built=CompileMdxModel(plain,{1,1},{"x","test","rgb"});
    const std::array assets{*built.value};auto made=PoseBatchEvaluator::Create(assets,1);auto evaluator=std::move(*made.value);
    std::array<PoseRequest,2> req{};for(std::uint32_t i=0;i<2;++i) { req[i].instance={i+1,1};req[i].asset={1,1};req[i].sequenceIndex=1;req[i].sequenceElapsedMs=i*500; }
    Check(evaluator.Evaluate(req).failedInstances==0,"双实例颜色失败");const auto first=evaluator.Colors(0)[1],second=evaluator.Colors(1)[1];
    Check(first!=second,"颜色串实例");Check(evaluator.Evaluate(req).failedInstances==0 && evaluator.Colors(0)[1]==first && evaluator.Colors(1)[1]==second,"颜色不确定");
    std::cout<<"PASS geoa-channels samples="<<samples<<" modes=4 global=1 bitwise_mismatches=0 new_calls=0 analytic_midpoints=1 boundary_unknown_nan_rejected=1\n";
}
}
int main() { try { Run();return 0; } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; } }
