#pragma once
#include "material_document.h"
#include "tinyengine/resource/material_definition_resource.h"
#include <array>
namespace tinyengine
{
class MaterialGraphEditor : public MaterialDocument<MaterialDefinitionData>
{
  public:
    bool Open(const std::string& path, const MaterialDefinitionData& data);
    std::uint32_t addNode(MaterialNodeType type, const std::string& name);
    bool connectPorts(std::uint32_t from, std::uint32_t output, std::uint32_t to, std::uint32_t input);
    bool disconnectPort(std::uint32_t to, std::uint32_t input);
    bool deleteNode(std::uint32_t id);
    bool setParameterDefault(const std::string& name, const MaterialParameterValue& value);
    std::vector<std::string> GetRetainedAssetReferences() const;
    bool undo();
    bool redo();
    bool canUndo() const
    {
        return !m_undo.empty() || m_editBefore.has_value();
    }
    bool canRedo() const
    {
        return !m_redo.empty();
    }
    void draw();

  private:
    std::uint32_t m_selected = 0, m_linkNode = 0, m_linkPort = 0;
    bool m_linkIsInput = false, m_fitRequested = false;
    float m_zoom = 1.f;
    ImVec2 m_pan{};
    ImVec2 m_canvasSize{};
    std::array<char, 128> m_search{};
    std::vector<MaterialDefinitionData> m_undo, m_redo;
    std::optional<MaterialDefinitionData> m_editBefore;
    bool validateDraft() override;
    void onDocumentReset() override;
    static std::string state(const MaterialDefinitionData& data);
    void recordEdit(MaterialDefinitionData before);
    void queueEdit(MaterialDefinitionData before);
    void commitPendingEdit();
    void updateDirty();
    void drawPalette(float width, float height);
    void drawCanvas();
    void drawProperties();
};
} // namespace tinyengine
