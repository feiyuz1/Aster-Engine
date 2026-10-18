#pragma once
#include "MaterialDocument.hpp"
#include "MiniEngine/Resource/MaterialDefinitionResource.hpp"
#include <array>
namespace MiniEngine
{
class MaterialGraphEditor : public MaterialDocument<MaterialDefinitionData>
{
  public:
    bool Open(const std::string &path, const MaterialDefinitionData &data);
    std::uint32_t AddNode(MaterialNodeType type, const std::string &name);
    bool ConnectPorts(std::uint32_t from, std::uint32_t output, std::uint32_t to, std::uint32_t input);
    bool DisconnectPort(std::uint32_t to, std::uint32_t input);
    bool DeleteNode(std::uint32_t id);
    bool SetParameterDefault(const std::string &name, const MaterialParameterValue &value);
    std::vector<std::string> GetRetainedAssetReferences() const;
    bool Undo();
    bool Redo();
    bool CanUndo() const
    {
        return !m_undo.empty() || m_editBefore.has_value();
    }
    bool CanRedo() const
    {
        return !m_redo.empty();
    }
    void Draw();

  private:
    std::uint32_t m_selected = 0, m_linkNode = 0, m_linkPort = 0;
    bool m_linkIsInput = false, m_fitRequested = false;
    float m_zoom = 1.f;
    ImVec2 m_pan{};
    ImVec2 m_canvasSize{};
    std::array<char, 128> m_search{};
    std::vector<MaterialDefinitionData> m_undo, m_redo;
    std::optional<MaterialDefinitionData> m_editBefore;
    bool ValidateDraft() override;
    void OnDocumentReset() override;
    static std::string State(const MaterialDefinitionData &data);
    void RecordEdit(MaterialDefinitionData before);
    void QueueEdit(MaterialDefinitionData before);
    void CommitPendingEdit();
    void UpdateDirty();
    void DrawPalette(float width, float height);
    void DrawCanvas();
    void DrawProperties();
};
} // namespace MiniEngine
