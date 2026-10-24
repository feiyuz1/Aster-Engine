#include "material_asset_editor.h"
#include "material_graph_editor.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/render/material_compiler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace tinyengine;
namespace
{
template <class T> auto retainedReferences(const T& editor, int) -> decltype(editor.GetRetainedAssetReferences())
{
    return editor.GetRetainedAssetReferences();
}
template <class T> std::vector<std::string> retainedReferences(const T&, ...)
{
    return {};
}
// Keep the first RED executable buildable against the old editor interface.
// These fallbacks make missing commands observable failures, never fake successes.
template <class T, class = void> struct Commands
{
    static void chinese(T&, bool) {}
    static bool undo(T&)
    {
        return false;
    }
    static bool redo(T&)
    {
        return false;
    }
    static void apply(T&, std::function<bool(const std::string&)>, bool = true) {}
    static bool focused(const T&)
    {
        return false;
    }
    static void hide(T&) {}
};
template <class T>
struct Commands<
    T, std::void_t<decltype(std::declval<T&>().setChinese(true)),
                   decltype(std::declval<T&>().setApplyRequest(std::function<bool(const std::string&)>{}, true)),
                   decltype(std::declval<const T&>().isFocused()), decltype(std::declval<T&>().hide())>>
{
    static void chinese(T& editor, bool value)
    {
        editor.setChinese(value);
    }
    static void apply(T& editor, std::function<bool(const std::string&)> callback, bool enabled = true)
    {
        editor.setApplyRequest(std::move(callback), enabled);
    }
    static bool focused(const T& editor)
    {
        return editor.isFocused();
    }
    static void hide(T& editor)
    {
        editor.hide();
    }
};

struct Harness
{
    int checks = 0;
    Harness()
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1600, 1000};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~Harness()
    {
        ImGui::DestroyContext();
    }
    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
        ++checks;
        std::cout << "PASS: " << message << '\n';
    }
    void frame(const std::function<void()>& draw)
    {
        ImGui::NewFrame();
        draw();
        ImGui::Render();
    }
    void Key(const std::function<void()>& draw, ImGuiKey key, bool ctrl = false, bool shift = false)
    {
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        io.AddKeyEvent(ImGuiMod_Shift, shift);
        io.AddKeyEvent(key, true);
        frame(draw);
        io.AddKeyEvent(key, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        frame(draw);
    }
    void mouse(const std::function<void()>& draw, ImVec2 position, int button, bool down)
    {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(position.x, position.y);
        io.AddMouseButtonEvent(button, down);
        frame(draw);
    }
    void click(const std::function<void()>& draw, ImVec2 position, int button = 0)
    {
        mouse(draw, position, button, true);
        mouse(draw, position, button, false);
    }
    std::string log(const std::function<void()>& draw)
    {
        ImGui::NewFrame();
        ImGui::LogToBuffer();
        draw();
        const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::Render();
        return text;
    }
    ImGuiWindow* root(const char* id)
    {
        if (auto* window = ImGui::FindWindowByID(ImHashStr(id)))
            return window;
        return ImGui::FindWindowByName("Material graph / 材质节点图");
    }
    ImGuiWindow* child(const char* name)
    {
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (window->Active && std::string(window->Name).find(std::string("/") + name + "_") != std::string::npos)
                return window;
        return nullptr;
    }
    void activate(const std::function<void()>& draw, ImGuiWindow* window, const char* id)
    {
        check(window != nullptr, "target material UI window exists");
        ImGui::ActivateItemByID(window->GetID(id));
        frame(draw);
        frame(draw);
    }
};
MaterialDefinitionData definition()
{
    MaterialDefinitionData data;
    data.graph.setFormatVersion(3);
    data.graph.addNode(MaterialNodeType::SurfaceOutput, "Surface", {350, 40});
    data.graph.addNode(MaterialNodeType::ComponentMask, "RGB", {40, 40});
    data.graph.GetNodes().back().channelMask = "xyz";
    data.graph.addNode(MaterialNodeType::Constant, "Color", {40, 240});
    return data;
}
void PaletteAndLanguage(Harness& h)
{
    MaterialGraphEditor editor;
    editor.Open("saved/MaterialEditorTests/palette.mat", definition());
    const auto draw = [&] { editor.draw(); };
    h.frame(draw);
    h.frame(draw);
    const auto english = h.log(draw);
    h.check(english.find("Node palette") != std::string::npos && english.find("Math") != std::string::npos,
            "graph exposes a persistent categorized node palette beside the canvas");
    Commands<MaterialGraphEditor>::chinese(editor, true);
    const auto chinese = h.log(draw);
    h.check(chinese.find("节点库") != std::string::npos && chinese.find("节点属性") != std::string::npos &&
                chinese.find("表面输出") != std::string::npos,
            "Chinese app setting localizes palette, node types and property controls");
    auto* palette = h.child("NodePalette");
    h.check(palette != nullptr, "node palette has a stable language independent UI identity");
    const auto before = editor.GetDraft().graph.GetNodes().size();
    h.activate(draw, palette, "Constant###CreateConstant");
    h.check(editor.GetDraft().graph.GetNodes().size() == before + 1 &&
                editor.GetDraft().graph.GetNodes().back().name == "常量",
            "Chinese palette creates a node with a localized display name");
    h.check(editor.GetDraft().graph.GetNodes().back().type == MaterialNodeType::Constant,
            "localized creation preserves serialized node type identifiers");
    Commands<MaterialGraphEditor>::chinese(editor, false);
    h.frame(draw);
    h.check(h.child("NodePalette") == palette, "changing language retains palette identity and state");
}
void UndoAndFocus(Harness& h)
{
    MaterialGraphEditor editor;
    auto data = definition();
    editor.Open("saved/MaterialEditorTests/undo.mat", data);
    const auto draw = [&] { editor.draw(); };
    h.frame(draw);
    h.frame(draw);
    auto* window = h.root("###MaterialGraphEditor");
    h.check(window != nullptr, "material graph window exists");
    ImGui::FocusWindow(window);
    editor.addNode(MaterialNodeType::Constant, "Transient");
    const auto count = editor.GetDraft().graph.GetNodes().size();
    h.Key(draw, ImGuiKey_Z, true);
    h.check(editor.GetDraft().graph.GetNodes().size() == count - 1 && !editor.isDirty(),
            "focused graph Ctrl+Z restores the saved baseline and clears dirty state");
    h.Key(draw, ImGuiKey_Y, true);
    h.check(editor.GetDraft().graph.GetNodes().size() == count && editor.isDirty(),
            "focused graph Ctrl+Y restores the edit and dirty state");
    h.Key(draw, ImGuiKey_Z, true);
    editor.addNode(MaterialNodeType::TexCoord, "Fresh");
    h.Key(draw, ImGuiKey_Y, true);
    h.check(editor.GetDraft().graph.GetNodes().size() == count &&
                editor.GetDraft().graph.GetNodes().back().name == "Fresh",
            "new edits invalidate the redo branch");
    auto* properties = h.child("NodeProperties");
    h.activate(draw, properties, "Name###NodeName");
    h.check(ImGui::IsAnyItemActive(), "node name field acquires real ImGui text ownership");
    h.Key(draw, ImGuiKey_Delete);
    h.Key(draw, ImGuiKey_Z, true);
    h.check(editor.GetDraft().graph.GetNodes().size() == count,
            "Delete and undo in a node text field do not issue graph commands");
    h.click(draw, {window->Pos.x + 80, window->Pos.y + 10});
    const auto modal = [&]
    {
        editor.draw();
        ImGui::Begin("Modal host");
        ImGui::OpenPopup("Editor modal");
        if (ImGui::BeginPopupModal("Editor modal"))
        {
            ImGui::TextUnformatted("Modal owns commands");
            ImGui::EndPopup();
        }
        ImGui::End();
    };
    h.frame(modal);
    h.frame(modal);
    h.Key(modal, ImGuiKey_Delete);
    h.Key(modal, ImGuiKey_Z, true);
    h.check(editor.GetDraft().graph.GetNodes().size() == count, "a modal dialog blocks graph Delete and undo commands");
    h.frame(
        [&]
        {
            editor.draw();
            ImGui::Begin("Modal host");
            if (ImGui::BeginPopupModal("Editor modal"))
            {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::End();
        });
    h.frame(
        [&]
        {
            editor.draw();
            ImGui::Begin("Other editor");
            ImGui::End();
        });
    ImGui::FocusWindow(ImGui::FindWindowByName("Other editor"));
    h.Key(draw, ImGuiKey_Delete);
    h.check(editor.GetDraft().graph.GetNodes().size() == count, "Delete outside the graph preserves its selected node");
    ImGui::FocusWindow(window);
    h.frame(draw);
    h.Key(draw, ImGuiKey_Delete);
    h.check(editor.GetDraft().graph.GetNodes().size() == count - 1,
            "Delete in the focused graph removes only its selected node");
    editor.Discard();
    h.frame(draw);
    h.check(!editor.isDirty(), "discard clears the undo baseline dirty state");
}
void CanvasInteractions(Harness& h)
{
    MaterialGraphEditor editor;
    editor.Open("saved/MaterialEditorTests/canvas.mat", definition());
    const auto draw = [&] { editor.draw(); };
    h.frame(draw);
    h.frame(draw);
    h.activate(draw, h.root("###MaterialGraphEditor"), "###ResetGraphView");
    auto* canvas = h.child("NodeCanvas");
    h.check(canvas != nullptr, "graph has a dedicated canvas surface");
    auto origin = canvas->DC.CursorStartPos;
    const ImVec2 from{origin.x + 240, origin.y + 88}, to{origin.x + 350, origin.y + 88};
    h.mouse(draw, from, 0, true);
    h.mouse(draw, to, 0, true);
    h.check(editor.GetDraft().graph.GetLinks().empty(),
            "dragging a port keeps a temporary wire without committing before release");
    h.mouse(draw, to, 0, false);
    h.check(editor.GetDraft().graph.GetLinks().size() == 1 && editor.GetDraft().graph.GetLinks()[0].fromNode == 2,
            "dragging a typed Vec3 output to the surface input creates a real graph connection");
    h.click(draw, to, 1);
    h.check(editor.GetDraft().graph.GetLinks().empty(), "right clicking an input disconnects its link");
    // A Vec4 cannot silently narrow into a surface Vec3 input.
    const ImVec2 wrong{origin.x + 240, origin.y + 288};
    h.mouse(draw, wrong, 0, true);
    h.mouse(draw, to, 0, true);
    h.mouse(draw, to, 0, false);
    h.check(editor.GetDraft().graph.GetLinks().empty() && !editor.GetDiagnostics().empty(),
            "incompatible drag connections preserve the graph and report type diagnostics");
    const auto initial = editor.GetDraft().graph.GetNodes()[1].position;
    h.mouse(draw, {origin.x + 90, origin.y + 55}, 0, true);
    h.mouse(draw, {origin.x + 120, origin.y + 75}, 0, true);
    h.mouse(draw, {origin.x + 150, origin.y + 95}, 0, true);
    h.mouse(draw, {origin.x + 150, origin.y + 95}, 0, false);
    h.check(editor.GetDraft().graph.GetNodes()[1].position.x > initial.x + 40,
            "dragging a node header moves graph coordinates");
    h.Key(draw, ImGuiKey_Z, true);
    h.check(std::abs(editor.GetDraft().graph.GetNodes()[1].position.x - initial.x) < .01f,
            "one undo coalesces an entire continuous node drag");
    const auto nodePosition = editor.GetDraft().graph.GetNodes()[1].position;
    h.mouse(draw, {origin.x + 300, origin.y + 330}, 2, true);
    h.mouse(draw, {origin.x + 340, origin.y + 355}, 2, true);
    h.mouse(draw, {origin.x + 340, origin.y + 355}, 2, false);
    h.check(editor.GetDraft().graph.GetNodes()[1].position.x == nodePosition.x,
            "canvas pan does not alter serialized node positions");
    // Zoom is visible in the percentage text and does not dirty the document.
    ImGui::GetIO().AddMouseWheelEvent(0, 1);
    const auto zoomed = h.log(draw);
    h.check(zoomed.find("110%") != std::string::npos || zoomed.find("115%") != std::string::npos,
            "mouse wheel over the canvas changes the displayed zoom");
    auto* root = h.root("###MaterialGraphEditor");
    h.activate(draw, root, "Fit graph###FitGraph");
    h.check(canvas->Scroll.x == 0 && canvas->Scroll.y == 0,
            "fit graph uses the graph view transform without scrolling its child window");
}
void CompactWindow(Harness& h)
{
    ImGui::GetIO().DisplaySize = {1280, 720};
    MaterialGraphEditor editor;
    editor.Open("saved/MaterialEditorTests/compact.mat", definition());
    const auto draw = [&] { editor.draw(); };
    h.frame(draw);
    h.frame(draw);
    const auto* window = h.root("###MaterialGraphEditor");
    const auto* viewport = ImGui::GetMainViewport();
    h.check(window && window->Pos.y >= viewport->WorkPos.y &&
                window->Pos.y + window->Size.y <= viewport->WorkPos.y + viewport->WorkSize.y &&
                window->Pos.x >= viewport->WorkPos.x &&
                window->Pos.x + window->Size.x <= viewport->WorkPos.x + viewport->WorkSize.x,
            "material document fits the available editor viewport on first open");
}
void DisconnectedTypes(Harness& h)
{
    MaterialGraphEditor editor;
    editor.Open("saved/MaterialEditorTests/disconnected.mat", definition());
    const auto texture = editor.addNode(MaterialNodeType::TextureParameter, "Texture");
    const auto add = editor.addNode(MaterialNodeType::Add, "Unconnected math");
    h.check(!editor.connectPorts(texture, 0, add, 0) && editor.GetDraft().graph.GetLinks().empty() &&
                !editor.GetDiagnostics().empty(),
            "disconnected arithmetic inputs reject a texture before a surface output makes them reachable");
    const auto uv = editor.addNode(MaterialNodeType::TexCoord, "UV");
    const auto sample = editor.addNode(MaterialNodeType::TextureSample, "Sample");
    h.check(!editor.connectPorts(texture, 0, sample, 1), "disconnected texture sampler UV input rejects Texture2D");
    h.check(editor.connectPorts(texture, 0, sample, 0) && editor.connectPorts(uv, 0, sample, 1),
            "disconnected texture and UV inputs accept their exact port types");
    h.check(editor.connectPorts(uv, 0, add, 0) && !editor.connectPorts(sample, 0, add, 1),
            "disconnected vector arithmetic rejects mismatched Vec2 and Vec4 inputs");
    const auto scalar = editor.addNode(MaterialNodeType::ScalarParameter, "Amount");
    h.check(editor.connectPorts(scalar, 0, add, 1), "disconnected vector arithmetic permits scalar broadcast");
    const auto lerp = editor.addNode(MaterialNodeType::Lerp, "Blend");
    const auto clamp = editor.addNode(MaterialNodeType::Clamp, "Limit");
    const auto normal = editor.addNode(MaterialNodeType::NormalDecode, "Normal");
    h.check(!editor.connectPorts(uv, 0, lerp, 2) && !editor.connectPorts(uv, 0, clamp, 1),
            "disconnected lerp alpha and clamp limits require scalar inputs");
    h.check(!editor.connectPorts(uv, 0, normal, 0) && editor.connectPorts(scalar, 0, normal, 0),
            "disconnected normal decode enforces Vec3 while retaining compiler scalar broadcast");
}
void SaveAndApply(Harness& h)
{
    const auto directory = std::filesystem::absolute("saved/MaterialEditorTests") / fromUtf8Path("中文材质");
    const auto path = toUtf8GenericPath((directory / "graph.mat"));
    MaterialGraphEditor editor;
    auto definitionValue = definition();
    definitionValue.sourcePath = path;
    editor.Open(path, definitionValue);
    editor.setSaveRequest(
        [](const auto& filename, const auto& draft, auto& diagnostics)
        {
            const auto compiled = MaterialCompiler{}.compile(draft);
            diagnostics = compiled.diagnostics;
            if (!compiled.Success())
                return false;
            auto graph = draft.graph;
            graph.GetParameters() = draft.parameters;
            graph.GetRenderState() = draft.renderState;
            return PlatformFile::writeAllText(fromUtf8Path(filename), graph.serialize());
        });
    bool applied = false;
    Commands<MaterialGraphEditor>::apply(editor,
                                         [&](const std::string& filename)
                                         {
                                             MaterialDefinitionResource saved;
                                             applied = filename == path && saved.load(filename) &&
                                                       saved.GetData().graph.GetNodes().size() == 4;
                                             return applied;
                                         });
    editor.addNode(MaterialNodeType::TexCoord, "UV");
    const auto draw = [&] { editor.draw(); };
    h.frame(draw);
    h.frame(draw);
    h.activate(draw, h.root("###MaterialGraphEditor"), "Apply to selected mesh###ApplyMaterial");
    h.check(applied && !editor.isDirty() && std::filesystem::exists(fromUtf8Path(path)),
            "Apply saves a valid dirty graph to a Unicode path before invoking mesh assignment");
    auto invalid = editor.GetDraft();
    invalid.graph.GetLinks().push_back({999, 1, 0, 0});
    editor.Open(path, invalid);
    editor.addNode(MaterialNodeType::Constant, "Broken");
    applied = false;
    h.activate(draw, h.root("###MaterialGraphEditor"), "Apply to selected mesh###ApplyMaterial");
    h.check(!applied && editor.isDirty() && !editor.GetDiagnostics().empty(),
            "invalid unsaved graph cannot invoke Apply or overwrite the last saved material");
    Commands<MaterialGraphEditor>::hide(editor);
    h.frame(draw);
    h.check(!Commands<MaterialGraphEditor>::focused(editor), "hiding a material document releases editor focus");
    MaterialAssetData instance;
    instance.formatVersion = 3;
    instance.parentPath = "builtin:PBR";
    MaterialAssetEditor asset;
    asset.Open(toUtf8GenericPath((directory / "child.mat")), instance, makeBuiltinPbrDefinition().parameters);
    Commands<MaterialAssetEditor>::chinese(asset, true);
    const auto text = h.log([&] { asset.draw(); });
    h.check(text.find("材质实例") != std::string::npos && text.find("覆盖") != std::string::npos &&
                text.find("父材质") != std::string::npos,
            "instance UI follows Chinese language while retaining inherited overrides");
    asset.setSaveRequest([](const auto& filename, const auto& draft, auto&)
                         { return PlatformFile::writeAllText(fromUtf8Path(filename), serializeMaterialAsset(draft)); });
    const auto assetDraw = [&] { asset.draw(); };
    const auto instancePath = asset.GetPath();
    bool instanceApplied = false;
    const auto applyInstance = [&](const std::string& filename)
    {
        MaterialResource saved;
        instanceApplied = filename == instancePath && saved.load(filename) &&
                          std::get<float>(saved.GetMaterialData().overrides.at("Metallic")) == .8f;
        return instanceApplied;
    };
    asset.setOverride("Metallic", .8f);
    Commands<MaterialAssetEditor>::apply(asset, applyInstance, false);
    h.frame(assetDraw);
    h.frame(assetDraw);
    h.activate(assetDraw, h.root("###MaterialAssetEditor"), "Apply to selected mesh###ApplyMaterial");
    h.check(!instanceApplied && asset.isDirty(), "instance Apply is disabled when no selected mesh can receive it");
    Commands<MaterialAssetEditor>::apply(asset, applyInstance);
    h.frame(assetDraw);
    h.activate(assetDraw, h.root("###MaterialAssetEditor"), "Apply to selected mesh###ApplyMaterial");
    h.check(instanceApplied && !asset.isDirty(),
            "instance Apply saves real typed overrides before assigning the selected mesh");
}
void FocusedSave(Harness& h)
{
    const auto path = toUtf8GenericPath((std::filesystem::absolute("saved/MaterialEditorTests") / "keyboard.mat"));
    MaterialGraphEditor editor;
    editor.Open(path, definition());
    editor.setSaveRequest(
        [](const auto& filename, const auto& draft, auto& diagnostics)
        {
            const auto result = MaterialCompiler{}.compile(draft);
            diagnostics = result.diagnostics;
            return result.Success() && PlatformFile::writeAllText(fromUtf8Path(filename), draft.graph.serialize());
        });
    editor.addNode(MaterialNodeType::TexCoord, "Keyboard UV");
    const auto draw = [&] { editor.draw(); };
    h.frame(draw);
    h.frame(draw);
    ImGui::FocusWindow(h.root("###MaterialGraphEditor"));
    h.Key(draw, ImGuiKey_S, true);
    MaterialDefinitionResource saved;
    h.check(!editor.isDirty() && saved.load(path) && saved.GetData().graph.GetNodes().size() == 4,
            "focused material Ctrl+S saves the actual material document");
}
void RetainedDocumentReferences(Harness& h)
{
    auto data = definition();
    MaterialParameterDecl texture;
    texture.name = "Map";
    texture.type = MaterialParameterType::Texture2D;
    texture.defaultValue = std::string("old.ppm");
    data.parameters.push_back(texture);
    MaterialGraphEditor graph;
    graph.Open("saved/MaterialEditorTests/references.mat", data);
    graph.setSaveRequest(
        [](const auto& filename, const auto& draft, auto& diagnostics)
        {
            const auto result = MaterialCompiler{}.compile(draft);
            diagnostics = result.diagnostics;
            auto serialized = draft.graph;
            serialized.GetParameters() = draft.parameters;
            return result.Success() && PlatformFile::writeAllText(fromUtf8Path(filename), serialized.serialize());
        });
    graph.setParameterDefault("Map", std::string("new.ppm"));
    h.check(graph.Save(), "save a texture parameter change while retaining its undo baseline");
    const auto graphReferences = retainedReferences(graph, 0);
    h.check(std::find(graphReferences.begin(), graphReferences.end(), "old.ppm") != graphReferences.end() &&
                std::find(graphReferences.begin(), graphReferences.end(), "new.ppm") != graphReferences.end(),
            "graph asset blockers include saved texture paths and prior defaults retained by undo");
    h.check(graph.undo() && std::get<std::string>(graph.GetDraft().parameters[0].defaultValue) == "old.ppm",
            "graph undo really restores the retained old texture reference");
    MaterialAssetData instance;
    instance.formatVersion = 3;
    instance.parentPath = "parent.mat";
    instance.overrides["BaseMap"] = std::string("old-instance.ppm");
    MaterialAssetEditor asset;
    asset.Open("saved/MaterialEditorTests/references.mat", instance, makeBuiltinPbrDefinition().parameters);
    asset.setOverride("BaseMap", std::string("new-instance.ppm"));
    const auto instanceReferences = retainedReferences(asset, 0);
    h.check(std::find(instanceReferences.begin(), instanceReferences.end(), "parent.mat") != instanceReferences.end() &&
                std::find(instanceReferences.begin(), instanceReferences.end(), "old-instance.ppm") !=
                    instanceReferences.end() &&
                std::find(instanceReferences.begin(), instanceReferences.end(), "new-instance.ppm") !=
                    instanceReferences.end(),
            "instance asset blockers include parent, draft texture and saved discard baseline");
    asset.Discard();
    h.check(std::get<std::string>(asset.GetDraft().overrides.at("BaseMap")) == "old-instance.ppm",
            "instance discard really restores the retained saved texture reference");
}
} // namespace
int main()
{
    int failures = 0, total = 0;
    for (const auto& test :
         std::vector<std::pair<const char*, void (*)(Harness&)>>{{"palette and language", PaletteAndLanguage},
                                                                 {"undo and focus", UndoAndFocus},
                                                                 {"canvas interactions", CanvasInteractions},
                                                                 {"disconnected types", DisconnectedTypes},
                                                                 {"save and apply", SaveAndApply},
                                                                 {"focused save", FocusedSave},
                                                                 {"retained references", RetainedDocumentReferences},
                                                                 {"compact window", CompactWindow}})
    {
        Harness harness;
        try
        {
            test.second(harness);
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "FAIL [" << test.first << "]: " << error.what() << '\n';
        }
        total += harness.checks;
    }
    std::cout << total << " material editor checks passed; " << failures << " groups failed\n";
    return failures ? 1 : 0;
}
