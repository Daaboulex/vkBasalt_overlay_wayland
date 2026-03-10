#include "../field_editor.hpp"
#include "../../../imgui/imgui.h"
#include "../../../imgui/imgui_internal.h"
#include <cmath>

namespace vkBasalt
{
    class FloatFieldEditor : public FieldEditor
    {
    public:
        bool render(EffectParam& param) override
        {
            auto& p = static_cast<FloatParam&>(param);
            bool changed = false;

            if (ImGui::SliderFloat(p.label.c_str(), &p.value, p.minValue, p.maxValue))
            {
                if (p.step > 0.0f)
                    p.value = std::round(p.value / p.step) * p.step;
                changed = true;
            }

            // Right-click context menu to reset
            if (ImGui::BeginPopupContextItem("##reset"))
            {
                if (ImGui::MenuItem("Reset to default"))
                {
                    resetToDefault(param);
                    changed = true;
                }
                ImGui::EndPopup();
            }

            return changed;
        }

        void resetToDefault(EffectParam& param) override
        {
            param.resetToDefault();
        }
    };

    REGISTER_FIELD_EDITOR(ParamType::Float, FloatFieldEditor)

} // namespace vkBasalt
