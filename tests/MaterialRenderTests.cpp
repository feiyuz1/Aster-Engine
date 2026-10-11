#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Render/MaterialCompiler.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

using namespace MiniEngine;
namespace
{
int failures = 0;
const std::string root = "Saved/MaterialRender/";
void Check(bool condition, const char *label)
{ std::cout << (condition ? "[PASS] " : "[FAIL] ") << label << '\n'; failures += !condition; }
bool Near(float a, float b, float tolerance=.004f) { return std::abs(a-b) < tolerance; }
MaterialDefinitionData ColorDefinition()
{
    MaterialDefinitionData data;
    data.sourcePath = "gpu:color"; data.graph.SetFormatVersion(2);
    data.renderState.shadingModel = MaterialShadingModel::Unlit;
    data.graph.AddNode(MaterialNodeType::SurfaceOutput,"Surface");
    MaterialParameterDecl p; p.name = "Color"; p.type = MaterialParameterType::Vec4; p.defaultValue = Vec4{1,0,0,1}; data.parameters.push_back(p);
    const auto parameter = data.graph.AddNode(MaterialNodeType::VectorParameter,"Color"); data.graph.GetNodes().back().parameterName = "Color";
    const auto mask = data.graph.AddNode(MaterialNodeType::ComponentMask,"RGB"); data.graph.GetNodes().back().channelMask = "xyz";
    data.graph.Connect(parameter,mask,0); data.graph.Connect(mask,1,0);
    return data;
}
std::shared_ptr<MaterialRenderProxy> Proxy(const MaterialDefinitionData &definition)
{
    const auto compiled = MaterialCompiler{}.Compile(definition);
    Check(compiled.Success(),"CPU candidate for GPU test compiles");
    if (!compiled.Success()) throw std::runtime_error("GPU fixture graph failed");
    auto proxy = std::make_shared<MaterialRenderProxy>(); proxy->compiled = compiled.material; proxy->state = definition.renderState;
    for (const auto &slot : proxy->compiled->layout.numericSlots)
    {
        const auto p = std::find_if(definition.parameters.begin(),definition.parameters.end(),[&](const auto &q) { return q.name == slot.name; });
        proxy->numericSlots.push_back(MaterialNumericSlot(p->defaultValue));
    }
    for (const auto &slot : proxy->compiled->layout.textureSlots) proxy->textures.push_back({slot.name,slot.slot,{},slot.sampling});
    return proxy;
}
MaterialDefinitionData TextureDefinition(TextureSampling sampling)
{
    auto data = ColorDefinition(); data.parameters.clear();
    data.graph.GetNodes().resize(1); data.graph.GetLinks().clear();
    MaterialParameterDecl p; p.name="Map"; p.type=MaterialParameterType::Texture2D; p.defaultValue=std::string{}; p.sampling=sampling; data.parameters.push_back(p);
    const auto handle = data.graph.AddNode(MaterialNodeType::TextureParameter,"Map"); data.graph.GetNodes().back().parameterName="Map";
    const auto sample = data.graph.AddNode(MaterialNodeType::TextureSample,"Sample"); data.graph.Connect(handle,sample,0);
    const auto mask = data.graph.AddNode(MaterialNodeType::ComponentMask,"RGB"); data.graph.GetNodes().back().channelMask="xyz"; data.graph.Connect(sample,mask,0);
    data.graph.Connect(mask,1,0);
    return data;
}
FrameView View()
{
    FrameView view;
    view.lightCount=1; view.lights[0].intensity=0;
    view.clusteredLights.tilesX=1; view.clusteredLights.tilesY=1; view.clusteredLights.slicesZ=1;
    view.clusteredLights.offsets={0}; view.clusteredLights.counts={1}; view.clusteredLights.lightIndices={0};
    for (auto &matrix : view.shadowViewProjections) matrix=Mat4::Identity();
    view.shadowCascadeSplits={1,10,100};
    return view;
}
Vec4 Draw(SdlGlRenderDevice &device, RenderItem &item, const FrameView &view)
{
    device.BeginFrame(view); device.Draw(item);
    if (!device.GetLastShaderError().empty()) std::cout << "Material draw diagnostic: " << device.GetLastShaderError() << '\n';
    const auto hdr=device.ReadHdrAttachmentForTests(0);
    Check(hdr.size()==64*64,"read real linear HDR target");
    if (hdr.size()!=64*64) return {};
    return hdr[32*64+32];
}
void TestSharedPrograms(SdlGlRenderDevice &device, RenderItem &item)
{
    auto graph=ColorDefinition(); item.materialProxy=Proxy(graph);
    const auto before=device.GetMaterialProgramCompileCountForTests(MaterialPass::Base);
    auto red=Draw(device,item,View());
    auto blue=std::make_shared<MaterialRenderProxy>(*item.materialProxy); blue->numericSlots[0]={0,0,1,1}; item.materialProxy=blue;
    auto bluePixel=Draw(device,item,View());
    Check(Near(red.x,1) && Near(red.z,0) && Near(bluePixel.x,0) && Near(bluePixel.z,1),"shared program renders independent red and blue values");
    Check(device.GetMaterialProgramCompileCountForTests(MaterialPass::Base)==before+1,"red and blue compile one Base variant");
    auto changed=ColorDefinition(); changed.graph.GetNodes().back().channelMask="zyx"; item.materialProxy=Proxy(changed); Draw(device,item,View());
    Check(device.GetMaterialProgramCompileCountForTests(MaterialPass::Base)==before+2,"changed graph expression compiles a new program");
}
void TestParameterUpload(SdlGlRenderDevice &device, RenderItem &item)
{
    auto graph=ColorDefinition();
    for (const auto &entry : std::map<std::string,MaterialParameterValue>{{"FloatValue",.125f},{"Vec2Value",Vec2{2,3}},{"Vec3Value",Vec3{4,5,6}}})
    { MaterialParameterDecl p; p.name=entry.first; p.type=GetMaterialParameterType(entry.second); p.defaultValue=entry.second; graph.parameters.push_back(p); }
    auto proxy=Proxy(graph); item.materialProxy=proxy; Draw(device,item,View());
    using GetIndexed=void(APIENTRY *)(GLenum,GLuint,GLint *);
    using BindBuffer=void(APIENTRY *)(GLenum,GLuint);
    using ReadBuffer=void(APIENTRY *)(GLenum,std::ptrdiff_t,std::ptrdiff_t,void *);
    const auto get=reinterpret_cast<GetIndexed>(SDL_GL_GetProcAddress("glGetIntegeri_v"));
    const auto bind=reinterpret_cast<BindBuffer>(SDL_GL_GetProcAddress("glBindBuffer"));
    const auto read=reinterpret_cast<ReadBuffer>(SDL_GL_GetProcAddress("glGetBufferSubData"));
    Check(get && bind && read,"GPU UBO readback functions available");
    if (!get || !bind || !read) return;
    GLint buffer=0; get(0x8A28,2,&buffer); Check(buffer!=0,"numeric UBO is bound at binding 2");
    std::array<Vec4,64> values{}; if (!buffer) return;
    bind(0x8A11,static_cast<GLuint>(buffer)); read(0x8A11,0,sizeof(values),values.data());
    const std::map<std::string,Vec4> expected={{"Color",{1,0,0,1}},{"FloatValue",{.125f,0,0,0}},{"Vec2Value",{2,3,0,0}},{"Vec3Value",{4,5,6,0}}};
    for (const auto &slot : proxy->compiled->layout.numericSlots)
    { const auto v=values[slot.slot], e=expected.at(slot.name); Check(Near(v.x,e.x) && Near(v.y,e.y) && Near(v.z,e.z) && Near(v.w,e.w),"GPU contains correctly padded typed parameter slot"); }
}
void TestTextureSemantics(SdlGlRenderDevice &device, RenderItem &item)
{
    auto texture=std::make_shared<TextureResource>(); Check(texture->Load(root+"gray.ppm"),"load gray texture");
    auto linear=Proxy(TextureDefinition(TextureSampling::Linear)); linear->textures[0].resource=texture; item.materialProxy=linear;
    auto linearPixel=Draw(device,item,View());
    auto srgb=Proxy(TextureDefinition(TextureSampling::SRgb)); srgb->textures[0].resource=texture; item.materialProxy=srgb;
    auto srgbPixel=Draw(device,item,View());
    Check(Near(linearPixel.x,128.f/255.f) && Near(srgbPixel.x,.21586f),"same image samples correctly as Linear and sRGB");
    const auto before=device.GetMaterialProgramCompileCountForTests(MaterialPass::Base);
    auto darker=std::make_shared<TextureResource>(); Check(darker->Load(root+"dark.ppm"),"load replacement texture");
    auto changed=std::make_shared<MaterialRenderProxy>(*linear); changed->textures[0].resource=darker; item.materialProxy=changed;
    const auto darkerPixel=Draw(device,item,View());
    Check(Near(darkerPixel.x,64.f/255.f) && device.GetMaterialProgramCompileCountForTests(MaterialPass::Base)==before,"texture path/value change reuses program");
}
void TestFallbackTexture(SdlGlRenderDevice &device, RenderItem &item)
{
    item.materialProxy=Proxy(TextureDefinition(TextureSampling::SRgb)); const auto color=Draw(device,item,View());
    Check(Near(color.x,1) && Near(color.y,1) && Near(color.z,1),"missing color texture falls back to white");
    auto normalGraph=TextureDefinition(TextureSampling::Normal);
    const auto decode=normalGraph.graph.AddNode(MaterialNodeType::NormalDecode,"Decode"); normalGraph.graph.Connect(normalGraph.graph.GetNodes()[3].id,decode,0); normalGraph.graph.Connect(decode,1,0);
    item.materialProxy=Proxy(normalGraph); const auto normal=Draw(device,item,View());
    Check(Near(normal.x,0) && Near(normal.y,0) && Near(normal.z,1),"missing normal texture decodes to exact flat normal");
    auto missing=Proxy(TextureDefinition(TextureSampling::SRgb)); missing->textures[0].requestedPath="missing-test.png";
    item.materialProxy=missing;
    const auto before=device.GetMaterialDiagnosticsForTests().size();
    Draw(device,item,View()); Draw(device,item,View());
    Check(device.GetMaterialDiagnosticsForTests().size()==before+1,"missing texture warns once across repeated draws");
}
void TestTextureBudget(SdlGlRenderDevice &device, RenderItem &item)
{
    auto graph=TextureDefinition(TextureSampling::Linear);
    for (int i=1;i<8;++i) { MaterialParameterDecl p; p.name="UnusedMap"+std::to_string(i); p.type=MaterialParameterType::Texture2D; p.defaultValue=std::string{}; graph.parameters.push_back(p); }
    auto proxy=Proxy(graph); item.materialProxy=proxy;
    auto view=View(); auto environment=std::make_shared<TextureResource>(); environment->Load(root+"gray.ppm");
    view.environmentIrradiance=environment; view.environmentPrefilter=environment; view.environmentBrdfLut=environment;
    Draw(device,item,view);
    using ActiveTexture=void(APIENTRY *)(GLenum);
    const auto active=reinterpret_cast<ActiveTexture>(SDL_GL_GetProcAddress("glActiveTexture"));
    bool complete=active!=nullptr;
    if (active) for (int unit=0;unit<16;++unit)
    { active(GL_TEXTURE0+unit); GLint binding=0; glGetIntegerv(unit<14 ? GL_TEXTURE_BINDING_2D : 0x8C2C,&binding); complete=complete && binding!=0; }
    Check(complete,"8 material, 3 shadow, 3 environment and 2 buffer units remain bound together");
    if (active) active(GL_TEXTURE0);
    Check(glGetError()==GL_NO_ERROR,"material binding and draws generate no GL errors");
}
void TestPassClippingAndState(SdlGlRenderDevice &device,RenderItem &item)
{
    auto graph=ColorDefinition(); graph.renderState.blendMode=MaterialBlendMode::Masked; graph.renderState.twoSided=true;
    auto uv=graph.graph.AddNode(MaterialNodeType::TexCoord,"UV");
    auto x=graph.graph.AddNode(MaterialNodeType::ComponentMask,"X"); graph.graph.GetNodes().back().channelMask="x";
    graph.graph.Connect(uv,x,0); graph.graph.Connect(x,1,6);
    item.materialProxy=Proxy(graph); Draw(device,item,View()); auto hdr=device.ReadHdrAttachmentForTests(0);
    Check(hdr[32*64+20].x<.02f && hdr[32*64+44].x>.9f,"Masked Base clips UV hole and keeps solid half");
    device.BeginShadowPass(View(),0); device.DrawShadow(item);
    auto left=device.ReadShadowDepthForTests(640,1024), right=device.ReadShadowDepthForTests(1408,1024); device.EndShadowPass();
    Check(left>.99f && right<.6f,"Masked Shadow clips the same UV hole");
    auto opaque=ColorDefinition(); opaque.renderState.twoSided=true; item.materialProxy=Proxy(opaque); item.model=ScaleMatrix({-1,1,1});
    auto back=Draw(device,item,View()); opaque.renderState.twoSided=false; item.materialProxy=Proxy(opaque); auto culled=Draw(device,item,View());
    opaque.renderState.twoSided=true; item.materialProxy=Proxy(opaque); auto again=Draw(device,item,View());
    Check(back.x>.9f && culled.x<.02f && again.x>.9f,"TwoSided state alternates without leaking cull state");
    item.model=Mat4::Identity();
}
void TestGeometryAndPbr(SdlGlRenderDevice &device,RenderItem &item)
{
    item.materialProxy=Proxy(ColorDefinition());
    device.BeginFrame(View()); device.DrawInstanced({item,item}); auto hdr=device.ReadHdrAttachmentForTests(0);
    Check(hdr[32*64+32].x>.9f,"Instanced compiled material survives compute culling and rebinding");
    auto weighted=std::make_shared<MeshResource>(); weighted->Load(root+"weighted.mesh"); auto mesh=item.mesh; item.mesh=weighted;
    item.skinMatrices={TranslationMatrix({.7f,0,0})}; Draw(device,item,View()); hdr=device.ReadHdrAttachmentForTests(0);
    Check(hdr[32*64+10].x<.02f && hdr[32*64+50].x>.9f,"Skinned Base uses current bone transform");
    auto two=std::make_shared<MaterialRenderProxy>(*item.materialProxy); two->state.twoSided=true; item.materialProxy=two;
    device.BeginShadowPass(View(),0); device.DrawShadow(item);
    Check(device.ReadShadowDepthForTests(320,1024)>.99f && device.ReadShadowDepthForTests(1600,1024)<.6f,"Skinned Shadow uses the same current bone transform"); device.EndShadowPass();
    item.mesh=mesh; item.skinMatrices.clear(); item.material=std::make_shared<MaterialResource>(); item.material->Load(root+"pbr.mat");
    auto view=View(); view.lights[0].intensity=3; view.lights[0].direction={0,0,-1};
    item.materialProxy.reset(); auto old=Draw(device,item,view); auto oldAmbient=device.ReadHdrAttachmentForTests(2)[32*64+32];
    ResourceManager resources; resources.Initialize(nullptr,root); MaterialSystem system(resources); item.materialProxy=system.BuildProxy("assets/pbr.mat");
    auto unified=Draw(device,item,view); auto ambient=device.ReadHdrAttachmentForTests(2)[32*64+32];
    const auto relative=[](float a,float b) { return std::abs(a-b)/std::max(.01f,std::abs(a)); };
    Check(relative(old.x,unified.x)<.01f && relative(old.y,unified.y)<.01f && relative(old.z,unified.z)<.01f && relative(oldAmbient.x,ambient.x)<.01f,"fixed PBR and unified PBR match linear HDR within one percent");
    auto graph=ColorDefinition(); graph.renderState.shadingModel=MaterialShadingModel::Lit; graph.parameters[0].defaultValue=Vec4{.4f,.2f,.1f,1};
    for (auto port:{1u,2u,5u})
    {
        auto id=graph.graph.AddNode(MaterialNodeType::Constant,"PBR value"); auto &node=graph.graph.GetNodes().back();
        node.constantType=port==5 ? MaterialParameterType::Vec3 : MaterialParameterType::Float;
        node.value=port==1 ? Vec4{.6f,0,0,0} : port==2 ? Vec4{.35f,0,0,0} : Vec4{.1f,.05f,.02f,0}; graph.graph.Connect(id,1,port);
    }
    item.materialProxy=Proxy(graph); const auto graphPixel=Draw(device,item,view);
    Check(relative(old.x,graphPixel.x)<.01f && relative(old.y,graphPixel.y)<.01f && relative(old.z,graphPixel.z)<.01f,"node graph PBR matches fixed PBR HDR within one percent");
    resources.Shutdown(); item.material.reset();
}
void TestGpuCandidateRollback(SdlGlRenderDevice &device,RenderItem &item)
{
    auto graph=ColorDefinition(); graph.graph.GetParameters()=graph.parameters; graph.graph.GetRenderState()=graph.renderState;
    PlatformFile::WriteAllText(root+"reload.matgraph",graph.graph.Serialize());
    PlatformFile::WriteAllText(root+"reload.mat","TINY_MAT 2\ndefinition \"reload.matgraph\"\n");
    ResourceManager resources; resources.Initialize(nullptr,root); MaterialSystem system(resources);
    auto active=system.BuildProxy("assets/reload.mat"); item.materialProxy=active; auto red=Draw(device,item,View());
    const auto disk=PlatformFile::ReadAllText(root+"reload.matgraph"); graph.parameters[0].defaultValue=Vec4{0,1,0,1};
    const auto result=system.ApplyDefinitionDraft("assets/reload.matgraph",graph,[&](const auto &compiled,auto &diagnostics) {
        auto invalid=compiled; invalid.surfaceSource+="\nthis_is_invalid_glsl;\n"; return device.ValidateMaterial(invalid,diagnostics);
    });
    item.materialProxy=system.BuildProxy("assets/reload.mat"); const auto retained=Draw(device,item,View());
    Check(!result.success && !result.diagnostics.empty() && item.materialProxy==active && Near(red.x,retained.x) && PlatformFile::ReadAllText(root+"reload.matgraph")==disk,"real GPU compile failure rolls back disk and complete live snapshot");
    const auto accept=[&](const auto &compiled,auto &diagnostics) { return device.ValidateMaterial(compiled,diagnostics); };
    Check(system.ApplyDefinitionDraft("assets/reload.matgraph",graph,accept).success,"fixed GPU candidate publishes all geometry/pass variants");
    const auto programs=device.GetMaterialProgramCompileCountForTests(MaterialPass::Base); graph.parameters[0].defaultValue=Vec4{0,0,1,1};
    Check(system.ApplyDefinitionDraft("assets/reload.matgraph",graph,accept).success && device.GetMaterialProgramCompileCountForTests(MaterialPass::Base)==programs,"default-only publication reuses GPU programs");
    item.materialProxy=system.BuildProxy("assets/reload.mat"); auto blue=Draw(device,item,View()); Check(blue.z>.9f,"published child actually renders new parent default");
    auto invalid=std::make_shared<CompiledMaterial>(*item.materialProxy->compiled); invalid->surfaceSource+="\ninvalid_first_load;\n";
    auto broken=std::make_shared<MaterialRenderProxy>(*item.materialProxy); broken->compiled=invalid; item.materialProxy=broken;
    const auto pink=Draw(device,item,View()); Check(pink.x>.9f && pink.z>.9f && pink.y<.02f,"first GPU failure renders magenta fallback");
    auto textured=TextureDefinition(TextureSampling::Linear); textured.parameters[0].defaultValue=std::string("snapshot.ppm"); textured.graph.GetParameters()=textured.parameters; textured.graph.GetRenderState()=textured.renderState;
    PlatformFile::WriteAllText(root+"textured.matgraph",textured.graph.Serialize()); PlatformFile::WriteAllText(root+"snapshot.ppm","P3\n1 1\n255\n64 64 64\n");
    item.materialProxy=system.BuildProxy("assets/textured.matgraph"); const auto textureBefore=item.materialProxy; auto dark=Draw(device,item,View());
    const auto texturePrograms=device.GetMaterialProgramCompileCountForTests(MaterialPass::Base);
    PlatformFile::WriteAllText(root+"snapshot.ppm","P3\n1 1\n255\n192 192 192\n");
    Check(system.Reload("assets/textured.matgraph",accept).success,"reload validates a same-path texture content replacement");
    item.materialProxy=system.BuildProxy("assets/textured.matgraph"); const auto light=Draw(device,item,View());
    Check(Near(dark.x,64.f/255) && Near(light.x,192.f/255) && textureBefore->textures[0].version!=item.materialProxy->textures[0].version,"new texture content publishes independently while old snapshot retains its resource");
    Check(device.GetMaterialProgramCompileCountForTests(MaterialPass::Base)<=texturePrograms+2,"texture replacement only validates missing geometry variants");
    resources.Shutdown();
}
}
int main()
{
    std::cout.setf(std::ios::unitbuf);
    try
    {
        PlatformFile::WriteAllText(root+"quad.mesh","v -0.8 -0.8 0\nv 0.8 -0.8 0\nv 0.8 0.8 0\nv -0.8 0.8 0\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
        PlatformFile::WriteAllText(root+"gray.ppm","P3\n1 1\n255\n128 128 128\n");
        PlatformFile::WriteAllText(root+"dark.ppm","P3\n1 1\n255\n64 64 64\n");
        auto weightedSource=PlatformFile::ReadAllText(root+"quad.mesh");
        weightedSource.insert(weightedSource.find("f 1/"),"vw 1 0 1 0 0 0 0 0 0\nvw 2 0 1 0 0 0 0 0 0\nvw 3 0 1 0 0 0 0 0 0\nvw 4 0 1 0 0 0 0 0 0\n");
        PlatformFile::WriteAllText(root+"weighted.mesh",weightedSource);
        PlatformFile::WriteAllText(root+"pbr.mat","color .4 .2 .1 1\nmetallic .6\nroughness .35\nemissive .1 .05 .02\n");
        SdlGlRenderDevice device(64,64,"Material GPU tests");
        Check(device.Initialize(),"initialize real OpenGL 4.3 context"); if (failures) return 1;
        SDL_HideWindow(static_cast<SDL_Window *>(device.GetNativeWindow()));
        std::cout << "GPU: " << glGetString(GL_RENDERER) << " / " << glGetString(GL_VERSION) << '\n';
        device.EnableSceneRenderTarget(true,64,64); device.SetTaaEnabled(false); device.SetBloomEnabled(false); device.SetGtaoEnabled(false); device.SetFxaaEnabled(false);
        RenderItem item; item.mesh=std::make_shared<MeshResource>(); Check(item.mesh->Load(root+"quad.mesh"),"load GPU test quad");
        TestSharedPrograms(device,item); TestParameterUpload(device,item); TestTextureSemantics(device,item); TestFallbackTexture(device,item); TestTextureBudget(device,item);
        TestPassClippingAndState(device,item); TestGeometryAndPbr(device,item);
        TestGpuCandidateRollback(device,item);
        device.Shutdown();
    }
    catch (const std::exception &error) { std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
    return failures ? 1 : 0;
}
