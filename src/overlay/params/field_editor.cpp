#include "field_editor.hpp"
#include "../../imgui/imgui.h"

namespace vkBasalt
{
    FieldEditorFactory& FieldEditorFactory::instance()
    {
        static FieldEditorFactory factory;
        return factory;
    }

    void FieldEditorFactory::registerEditor(ParamType type, CreatorFunc creator)
    {
        creators[type] = creator;
    }

    FieldEditor* FieldEditorFactory::getEditor(ParamType type)
    {
        auto it = editors.find(type);
        if (it != editors.end())
            return it->second.get();

        auto creatorIt = creators.find(type);
        if (creatorIt == creators.end())
            return nullptr;

        editors[type] = creatorIt->second();
        return editors[type].get();
    }

    bool renderFieldEditor(EffectParam& param)
    {
        FieldEditor* editor = FieldEditorFactory::instance().getEditor(param.getType());
        if (!editor)
            return false;

        bool changed = editor->render(param);

        // Query hover state before opening the shared context popup below,
        // since popup handling changes ImGui's last-item state.
        if (!param.tooltip.empty()
            && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(param.tooltip.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        if (ImGui::BeginPopupContextItem("##reset_to_default"))
        {
            if (ImGui::MenuItem("Reset to default"))
            {
                editor->resetToDefault(param);
                changed = true;
            }
            ImGui::EndPopup();
        }

        return changed;
    }

} // namespace vkBasalt
