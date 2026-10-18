#include "MaterialAssetEditor.hpp"
#include "MaterialGraphEditor.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Render/MaterialCompiler.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace MiniEngine;
namespace
{
template <class T> auto RetainedReferences(const T &editor, int) -> decltype(editor.GetRetainedAssetReferences())
{
    return editor.GetRetainedAssetReferences();
}
template <class T> std::vector<std::string> RetainedReferences(const T &, ...)
{
    return {};
}
// Keep the first RED executable buildable against the old editor interface.
// These fallbacks make missing commands observable failures, never fake successes.
template <class T, class = void> struct Commands
{
    static void Chinese(T &, bool)
    {
    }
    static bool Undo(T &)
    {
        return false;
    }
    static bool Redo(T &)
    {
        return false;
    }
    static void Apply(T &, std::function<bool(const std::string &)>, bool = true)
    {
    }
    static bool Focused(const T &)
    {
        return false;
    }
    static void Hide(T &)
    {
    }
};
template <class T>
struct Commands<T, std::void_t<decltype(std::declval<T &>().SetChinese(true)),
                               decltype(std::declval<T &>().SetApplyRequest(std::function<bool(const std::string &)>{}, true)),
                               decltype(std::declval<const T &>().IsFocused()), decltype(std::declval<T &>().Hide())>>
{
    static void Chinese(T &editor, bool value)
    {
        editor.SetChinese(value);
    }
    static void Apply(T &editor, std::function<bool(const std::string &)> callback, bool enabled = true)
    {
        editor.SetApplyRequest(std::move(callback), enabled);
    }
    static bool Focused(const T &editor)
    {
        return editor.IsFocused();
    }
    static void Hide(T &editor)
    {
        editor.Hide();
    }
};

struct Harness
{
    int checks = 0;
    Harness()
    {
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1600, 1000};
        io.DeltaTime = 1.f / 60;
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~Harness()
    {
        ImGui::DestroyContext();
    }
    void Check(bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
        ++checks;
        std::cout << "PASS: " << message << '\n';
    }
    void Frame(const std::function<void()> &draw)
    {
        ImGui::NewFrame();
        draw();
        ImGui::Render();
    }
    void Key(const std::function<void()> &draw, ImGuiKey key, bool ctrl = false, bool shift = false)
    {
        auto &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        io.AddKeyEvent(ImGuiMod_Shift, shift);
        io.AddKeyEvent(key, true);
        Frame(draw);
        io.AddKeyEvent(key, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        Frame(draw);
    }
    void Mouse(const std::function<void()> &draw, ImVec2 position, int button, bool down)
    {
        auto &io = ImGui::GetIO();
        io.AddMousePosEvent(position.x, position.y);
        io.AddMouseButtonEvent(button, down);
        Frame(draw);
    }
    void Click(const std::function<void()> &draw, ImVec2 position, int button = 0)
    {
        Mouse(draw, position, button, true);
        Mouse(draw, position, button, false);
    }
    std::string Log(const std::function<void()> &draw)
    {
        ImGui::NewFrame();
        ImGui::LogToBuffer();
        draw();
        const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::Render();
        return text;
    }
    ImGuiWindow *Root(const char *id)
    {
        if (auto *window = ImGui::FindWindowByID(ImHashStr(id)))
            return window;
        return ImGui::FindWindowByName("Material graph / 材质节点图");
    }
    ImGuiWindow *Child(const char *name)
    {
        for (auto *window : ImGui::GetCurrentContext()->Windows)
            if (window->Active && std::string(window->Name).find(std::string("/") + name + "_") != std::string::npos)
                return window;
        return nullptr;
    }
    void Activate(const std::function<void()> &draw, ImGuiWindow *window, const char *id)
    {
        Check(window != nullptr, "target material UI window exists");
        ImGui::ActivateItemByID(window->GetID(id));
        Frame(draw);
        Frame(draw);
    }
};
MaterialDefinitionData Definition()
{
    MaterialDefinitionData data;
    data.graph.SetFormatVersion(2);
    data.graph.AddNode(MaterialNodeType::SurfaceOutput, "Surface", {350, 40});
    data.graph.AddNode(MaterialNodeType::ComponentMask, "RGB", {40, 40});
    data.graph.GetNodes().back().channelMask = "xyz";
    data.graph.AddNode(MaterialNodeType::Constant, "Color", {40, 240});
    return data;
}
void PaletteAndLanguage(Harness &h)
{
    MaterialGraphEditor editor;
    editor.Open("Saved/MaterialEditorTests/palette.matgraph", Definition());
    const auto draw = [&] { editor.Draw(); };
    h.Frame(draw);
    h.Frame(draw);
    const auto english = h.Log(draw);
    h.Check(english.find("Node palette") != std::string::npos && english.find("Math") != std::string::npos,
            "graph exposes a persistent categorized node palette beside the canvas");
    Commands<MaterialGraphEditor>::Chinese(editor, true);
    const auto chinese = h.Log(draw);
    h.Check(chinese.find("节点库") != std::string::npos && chinese.find("节点属性") != std::string::npos &&
                chinese.find("表面输出") != std::string::npos,
            "Chinese app setting localizes palette, node types and property controls");
    auto *palette = h.Child("NodePalette");
    h.Check(palette != nullptr, "node palette has a stable language independent UI identity");
    const auto before = editor.GetDraft().graph.GetNodes().size();
    h.Activate(draw, palette, "Constant###CreateConstant");
    h.Check(editor.GetDraft().graph.GetNodes().size() == before + 1 && editor.GetDraft().graph.GetNodes().back().name == "常量",
            "Chinese palette creates a node with a localized display name");
    h.Check(editor.GetDraft().graph.GetNodes().back().type == MaterialNodeType::Constant,
            "localized creation preserves serialized node type identifiers");
    Commands<MaterialGraphEditor>::Chinese(editor, false);
    h.Frame(draw);
    h.Check(h.Child("NodePalette") == palette, "changing language retains palette identity and state");
}
void UndoAndFocus(Harness &h)
{
    MaterialGraphEditor editor;
    auto data = Definition();
    editor.Open("Saved/MaterialEditorTests/undo.matgraph", data);
    const auto draw = [&] { editor.Draw(); };
    h.Frame(draw);
    h.Frame(draw);
    auto *window = h.Root("###MaterialGraphEditor");
    h.Check(window != nullptr, "material graph window exists");
    ImGui::FocusWindow(window);
    editor.AddNode(MaterialNodeType::Constant, "Transient");
    const auto count = editor.GetDraft().graph.GetNodes().size();
    h.Key(draw, ImGuiKey_Z, true);
    h.Check(editor.GetDraft().graph.GetNodes().size() == count - 1 && !editor.IsDirty(),
            "focused graph Ctrl+Z restores the saved baseline and clears dirty state");
    h.Key(draw, ImGuiKey_Y, true);
    h.Check(editor.GetDraft().graph.GetNodes().size() == count && editor.IsDirty(),
            "focused graph Ctrl+Y restores the edit and dirty state");
    h.Key(draw, ImGuiKey_Z, true);
    editor.AddNode(MaterialNodeType::TexCoord, "Fresh");
    h.Key(draw, ImGuiKey_Y, true);
    h.Check(editor.GetDraft().graph.GetNodes().size() == count && editor.GetDraft().graph.GetNodes().back().name == "Fresh",
            "new edits invalidate the redo branch");
    auto *properties = h.Child("NodeProperties");
    h.Activate(draw, properties, "Name###NodeName");
    h.Check(ImGui::IsAnyItemActive(), "node name field acquires real ImGui text ownership");
    h.Key(draw, ImGuiKey_Delete);
    h.Key(draw, ImGuiKey_Z, true);
    h.Check(editor.GetDraft().graph.GetNodes().size() == count, "Delete and undo in a node text field do not issue graph commands");
    h.Click(draw, {window->Pos.x + 80, window->Pos.y + 10});
    const auto modal = [&] {
        editor.Draw();
        ImGui::Begin("Modal host");
        ImGui::OpenPopup("Editor modal");
        if (ImGui::BeginPopupModal("Editor modal"))
        {
            ImGui::TextUnformatted("Modal owns commands");
            ImGui::EndPopup();
        }
        ImGui::End();
    };
    h.Frame(modal);
    h.Frame(modal);
    h.Key(modal, ImGuiKey_Delete);
    h.Key(modal, ImGuiKey_Z, true);
    h.Check(editor.GetDraft().graph.GetNodes().size() == count, "a modal dialog blocks graph Delete and undo commands");
    h.Frame([&] {
        editor.Draw();
        ImGui::Begin("Modal host");
        if (ImGui::BeginPopupModal("Editor modal"))
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::End();
    });
    h.Frame([&] {
        editor.Draw();
        ImGui::Begin("Other editor");
        ImGui::End();
    });
    ImGui::FocusWindow(ImGui::FindWindowByName("Other editor"));
    h.Key(draw, ImGuiKey_Delete);
    h.Check(editor.GetDraft().graph.GetNodes().size() == count, "Delete outside the graph preserves its selected node");
    ImGui::FocusWindow(window);
    h.Frame(draw);
    h.Key(draw, ImGuiKey_Delete);
    h.Check(editor.GetDraft().graph.GetNodes().size() == count - 1, "Delete in the focused graph removes only its selected node");
    editor.Discard();
    h.Frame(draw);
    h.Check(!editor.IsDirty(), "discard clears the undo baseline dirty state");
}
void CanvasInteractions(Harness &h)
{
    MaterialGraphEditor editor;
    editor.Open("Saved/MaterialEditorTests/canvas.matgraph", Definition());
    const auto draw = [&] { editor.Draw(); };
    h.Frame(draw);
    h.Frame(draw);
    h.Activate(draw, h.Root("###MaterialGraphEditor"), "###ResetGraphView");
    auto *canvas = h.Child("NodeCanvas");
    h.Check(canvas != nullptr, "graph has a dedicated canvas surface");
    auto origin = canvas->DC.CursorStartPos;
    const ImVec2 from{origin.x + 240, origin.y + 88}, to{origin.x + 350, origin.y + 88};
    h.Mouse(draw, from, 0, true);
    h.Mouse(draw, to, 0, true);
    h.Check(editor.GetDraft().graph.GetLinks().empty(), "dragging a port keeps a temporary wire without committing before release");
    h.Mouse(draw, to, 0, false);
    h.Check(editor.GetDraft().graph.GetLinks().size() == 1 && editor.GetDraft().graph.GetLinks()[0].fromNode == 2,
            "dragging a typed Vec3 output to the surface input creates a real graph connection");
    h.Click(draw, to, 1);
    h.Check(editor.GetDraft().graph.GetLinks().empty(), "right clicking an input disconnects its link");
    // A Vec4 cannot silently narrow into a surface Vec3 input.
    const ImVec2 wrong{origin.x + 240, origin.y + 288};
    h.Mouse(draw, wrong, 0, true);
    h.Mouse(draw, to, 0, true);
    h.Mouse(draw, to, 0, false);
    h.Check(editor.GetDraft().graph.GetLinks().empty() && !editor.GetDiagnostics().empty(),
            "incompatible drag connections preserve the graph and report type diagnostics");
    const auto initial = editor.GetDraft().graph.GetNodes()[1].position;
    h.Mouse(draw, {origin.x + 90, origin.y + 55}, 0, true);
    h.Mouse(draw, {origin.x + 120, origin.y + 75}, 0, true);
    h.Mouse(draw, {origin.x + 150, origin.y + 95}, 0, true);
    h.Mouse(draw, {origin.x + 150, origin.y + 95}, 0, false);
    h.Check(editor.GetDraft().graph.GetNodes()[1].position.x > initial.x + 40, "dragging a node header moves graph coordinates");
    h.Key(draw, ImGuiKey_Z, true);
    h.Check(std::abs(editor.GetDraft().graph.GetNodes()[1].position.x - initial.x) < .01f,
            "one undo coalesces an entire continuous node drag");
    const auto nodePosition = editor.GetDraft().graph.GetNodes()[1].position;
    h.Mouse(draw, {origin.x + 300, origin.y + 330}, 2, true);
    h.Mouse(draw, {origin.x + 340, origin.y + 355}, 2, true);
    h.Mouse(draw, {origin.x + 340, origin.y + 355}, 2, false);
    h.Check(editor.GetDraft().graph.GetNodes()[1].position.x == nodePosition.x, "canvas pan does not alter serialized node positions");
    // Zoom is visible in the percentage text and does not dirty the document.
    ImGui::GetIO().AddMouseWheelEvent(0, 1);
    const auto zoomed = h.Log(draw);
    h.Check(zoomed.find("110%") != std::string::npos || zoomed.find("115%") != std::string::npos,
            "mouse wheel over the canvas changes the displayed zoom");
    auto *root = h.Root("###MaterialGraphEditor");
    h.Activate(draw, root, "Fit graph###FitGraph");
    h.Check(canvas->Scroll.x == 0 && canvas->Scroll.y == 0, "fit graph uses the graph view transform without scrolling its child window");
}
void CompactWindow(Harness &h)
{
    ImGui::GetIO().DisplaySize = {1280, 720};
    MaterialGraphEditor editor;
    editor.Open("Saved/MaterialEditorTests/compact.matgraph", Definition());
    const auto draw = [&] { editor.Draw(); };
    h.Frame(draw);
    h.Frame(draw);
    const auto *window = h.Root("###MaterialGraphEditor");
    const auto *viewport = ImGui::GetMainViewport();
    h.Check(window && window->Pos.y >= viewport->WorkPos.y &&
                window->Pos.y + window->Size.y <= viewport->WorkPos.y + viewport->WorkSize.y && window->Pos.x >= viewport->WorkPos.x &&
                window->Pos.x + window->Size.x <= viewport->WorkPos.x + viewport->WorkSize.x,
            "material document fits the available editor viewport on first open");
}
void DisconnectedTypes(Harness &h)
{
    MaterialGraphEditor editor;
    editor.Open("Saved/MaterialEditorTests/disconnected.matgraph", Definition());
    const auto texture = editor.AddNode(MaterialNodeType::TextureParameter, "Texture");
    const auto add = editor.AddNode(MaterialNodeType::Add, "Unconnected math");
    h.Check(!editor.ConnectPorts(texture, 0, add, 0) && editor.GetDraft().graph.GetLinks().empty() && !editor.GetDiagnostics().empty(),
            "disconnected arithmetic inputs reject a texture before a surface output makes them reachable");
    const auto uv = editor.AddNode(MaterialNodeType::TexCoord, "UV");
    const auto sample = editor.AddNode(MaterialNodeType::TextureSample, "Sample");
    h.Check(!editor.ConnectPorts(texture, 0, sample, 1), "disconnected texture sampler UV input rejects Texture2D");
    h.Check(editor.ConnectPorts(texture, 0, sample, 0) && editor.ConnectPorts(uv, 0, sample, 1),
            "disconnected texture and UV inputs accept their exact port types");
    h.Check(editor.ConnectPorts(uv, 0, add, 0) && !editor.ConnectPorts(sample, 0, add, 1),
            "disconnected vector arithmetic rejects mismatched Vec2 and Vec4 inputs");
    const auto scalar = editor.AddNode(MaterialNodeType::ScalarParameter, "Amount");
    h.Check(editor.ConnectPorts(scalar, 0, add, 1), "disconnected vector arithmetic permits scalar broadcast");
    const auto lerp = editor.AddNode(MaterialNodeType::Lerp, "Blend");
    const auto clamp = editor.AddNode(MaterialNodeType::Clamp, "Limit");
    const auto normal = editor.AddNode(MaterialNodeType::NormalDecode, "Normal");
    h.Check(!editor.ConnectPorts(uv, 0, lerp, 2) && !editor.ConnectPorts(uv, 0, clamp, 1),
            "disconnected lerp alpha and clamp limits require scalar inputs");
    h.Check(!editor.ConnectPorts(uv, 0, normal, 0) && editor.ConnectPorts(scalar, 0, normal, 0),
            "disconnected normal decode enforces Vec3 while retaining compiler scalar broadcast");
}
void SaveAndApply(Harness &h)
{
    const auto directory = std::filesystem::absolute("Saved/MaterialEditorTests") / std::filesystem::u8path("中文材质");
    const auto path = (directory / "graph.matgraph").generic_u8string();
    MaterialGraphEditor editor;
    auto definition = Definition();
    definition.sourcePath = path;
    editor.Open(path, definition);
    editor.SetSaveRequest([](const auto &filename, const auto &draft, auto &diagnostics) {
        const auto compiled = MaterialCompiler{}.Compile(draft);
        diagnostics = compiled.diagnostics;
        if (!compiled.Success())
            return false;
        auto graph = draft.graph;
        graph.GetParameters() = draft.parameters;
        graph.GetRenderState() = draft.renderState;
        return PlatformFile::WriteAllText(std::filesystem::u8path(filename), graph.Serialize());
    });
    bool applied = false;
    Commands<MaterialGraphEditor>::Apply(editor, [&](const std::string &filename) {
        MaterialDefinitionResource saved;
        applied = filename == path && saved.Load(filename) && saved.GetData().graph.GetNodes().size() == 4;
        return applied;
    });
    editor.AddNode(MaterialNodeType::TexCoord, "UV");
    const auto draw = [&] { editor.Draw(); };
    h.Frame(draw);
    h.Frame(draw);
    h.Activate(draw, h.Root("###MaterialGraphEditor"), "Apply to selected mesh###ApplyMaterial");
    h.Check(applied && !editor.IsDirty() && std::filesystem::exists(std::filesystem::u8path(path)),
            "Apply saves a valid dirty graph to a Unicode path before invoking mesh assignment");
    auto invalid = editor.GetDraft();
    invalid.graph.GetLinks().push_back({999, 1, 0, 0});
    editor.Open(path, invalid);
    editor.AddNode(MaterialNodeType::Constant, "Broken");
    applied = false;
    h.Activate(draw, h.Root("###MaterialGraphEditor"), "Apply to selected mesh###ApplyMaterial");
    h.Check(!applied && editor.IsDirty() && !editor.GetDiagnostics().empty(),
            "invalid unsaved graph cannot invoke Apply or overwrite the last saved material");
    Commands<MaterialGraphEditor>::Hide(editor);
    h.Frame(draw);
    h.Check(!Commands<MaterialGraphEditor>::Focused(editor), "hiding a material document releases editor focus");
    MaterialAssetData instance;
    instance.formatVersion = 2;
    instance.definitionPath = "builtin:PBR";
    MaterialAssetEditor asset;
    asset.Open((directory / "child.mat").generic_u8string(), instance, MakeBuiltinPbrDefinition().parameters);
    Commands<MaterialAssetEditor>::Chinese(asset, true);
    const auto text = h.Log([&] { asset.Draw(); });
    h.Check(text.find("材质实例") != std::string::npos && text.find("覆盖") != std::string::npos &&
                text.find("父材质") != std::string::npos,
            "instance UI follows Chinese language while retaining inherited overrides");
    asset.SetSaveRequest([](const auto &filename, const auto &draft, auto &) {
        return PlatformFile::WriteAllText(std::filesystem::u8path(filename), SerializeMaterialAsset(draft));
    });
    const auto assetDraw = [&] { asset.Draw(); };
    const auto instancePath = asset.GetPath();
    bool instanceApplied = false;
    const auto applyInstance = [&](const std::string &filename) {
        MaterialResource saved;
        instanceApplied =
            filename == instancePath && saved.Load(filename) && std::get<float>(saved.GetMaterialData().overrides.at("Metallic")) == .8f;
        return instanceApplied;
    };
    asset.SetOverride("Metallic", .8f);
    Commands<MaterialAssetEditor>::Apply(asset, applyInstance, false);
    h.Frame(assetDraw);
    h.Frame(assetDraw);
    h.Activate(assetDraw, h.Root("###MaterialAssetEditor"), "Apply to selected mesh###ApplyMaterial");
    h.Check(!instanceApplied && asset.IsDirty(), "instance Apply is disabled when no selected mesh can receive it");
    Commands<MaterialAssetEditor>::Apply(asset, applyInstance);
    h.Frame(assetDraw);
    h.Activate(assetDraw, h.Root("###MaterialAssetEditor"), "Apply to selected mesh###ApplyMaterial");
    h.Check(instanceApplied && !asset.IsDirty(), "instance Apply saves real typed overrides before assigning the selected mesh");
}
void FocusedSave(Harness &h)
{
    const auto path = (std::filesystem::absolute("Saved/MaterialEditorTests") / "keyboard.matgraph").generic_u8string();
    MaterialGraphEditor editor;
    editor.Open(path, Definition());
    editor.SetSaveRequest([](const auto &filename, const auto &draft, auto &diagnostics) {
        const auto result = MaterialCompiler{}.Compile(draft);
        diagnostics = result.diagnostics;
        return result.Success() && PlatformFile::WriteAllText(std::filesystem::u8path(filename), draft.graph.Serialize());
    });
    editor.AddNode(MaterialNodeType::TexCoord, "Keyboard UV");
    const auto draw = [&] { editor.Draw(); };
    h.Frame(draw);
    h.Frame(draw);
    ImGui::FocusWindow(h.Root("###MaterialGraphEditor"));
    h.Key(draw, ImGuiKey_S, true);
    MaterialDefinitionResource saved;
    h.Check(!editor.IsDirty() && saved.Load(path) && saved.GetData().graph.GetNodes().size() == 4,
            "focused material Ctrl+S saves the actual material document");
}
void RetainedDocumentReferences(Harness &h)
{
    auto data = Definition();
    MaterialParameterDecl texture;
    texture.name = "Map";
    texture.type = MaterialParameterType::Texture2D;
    texture.defaultValue = std::string("old.ppm");
    data.parameters.push_back(texture);
    MaterialGraphEditor graph;
    graph.Open("Saved/MaterialEditorTests/references.matgraph", data);
    graph.SetSaveRequest([](const auto &filename, const auto &draft, auto &diagnostics) {
        const auto result = MaterialCompiler{}.Compile(draft);
        diagnostics = result.diagnostics;
        auto serialized = draft.graph;
        serialized.GetParameters() = draft.parameters;
        return result.Success() && PlatformFile::WriteAllText(std::filesystem::u8path(filename), serialized.Serialize());
    });
    graph.SetParameterDefault("Map", std::string("new.ppm"));
    h.Check(graph.Save(), "save a texture parameter change while retaining its undo baseline");
    const auto graphReferences = RetainedReferences(graph, 0);
    h.Check(std::find(graphReferences.begin(), graphReferences.end(), "old.ppm") != graphReferences.end() &&
                std::find(graphReferences.begin(), graphReferences.end(), "new.ppm") != graphReferences.end(),
            "graph asset blockers include saved texture paths and prior defaults retained by undo");
    h.Check(graph.Undo() && std::get<std::string>(graph.GetDraft().parameters[0].defaultValue) == "old.ppm",
            "graph undo really restores the retained old texture reference");
    MaterialAssetData instance;
    instance.formatVersion = 2;
    instance.parentPath = "parent.mat";
    instance.overrides["BaseMap"] = std::string("old-instance.ppm");
    MaterialAssetEditor asset;
    asset.Open("Saved/MaterialEditorTests/references.mat", instance, MakeBuiltinPbrDefinition().parameters);
    asset.SetOverride("BaseMap", std::string("new-instance.ppm"));
    const auto instanceReferences = RetainedReferences(asset, 0);
    h.Check(std::find(instanceReferences.begin(), instanceReferences.end(), "parent.mat") != instanceReferences.end() &&
                std::find(instanceReferences.begin(), instanceReferences.end(), "old-instance.ppm") != instanceReferences.end() &&
                std::find(instanceReferences.begin(), instanceReferences.end(), "new-instance.ppm") != instanceReferences.end(),
            "instance asset blockers include parent, draft texture and saved discard baseline");
    asset.Discard();
    h.Check(std::get<std::string>(asset.GetDraft().overrides.at("BaseMap")) == "old-instance.ppm",
            "instance discard really restores the retained saved texture reference");
}
} // namespace
int main()
{
    int failures = 0, total = 0;
    for (const auto &test : std::vector<std::pair<const char *, void (*)(Harness &)>>{{"palette and language", PaletteAndLanguage},
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
        catch (const std::exception &error)
        {
            ++failures;
            std::cerr << "FAIL [" << test.first << "]: " << error.what() << '\n';
        }
        total += harness.checks;
    }
    std::cout << total << " material editor checks passed; " << failures << " groups failed\n";
    return failures ? 1 : 0;
}
