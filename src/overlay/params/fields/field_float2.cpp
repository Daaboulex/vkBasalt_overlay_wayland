#include "../field_editor.hpp"
#include "../../../imgui/imgui.h"
#include "../../../imgui/imgui_internal.h"
#include <cmath>

namespace vkBasalt
{
    class Float2FieldEditor : public FieldEditor
    {
    public:
        bool render(EffectParam& param) override
        {
            auto& p = static_cast<Float2Param&>(param);
            bool changed = false;

            // Use the min/max from the first component for now
            // TODO: Could support per-component ranges if needed
            if (ImGui::SliderFloat2(p.label.c_str(), p.value, p.minValue[0], p.maxValue[0]))
            {
                if (p.step > 0.0f)
                {
                    p.value[0] = std::round(p.value[0] / p.step) * p.step;
                    p.value[1] = std::round(p.value[1] / p.step) * p.step;
                }
                changed = true;
            }

            // Double-click to reset
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                resetToDefault(param);
                changed = true;
                ImGui::ClearActiveID();
            }

            return changed;
        }

        void resetToDefault(EffectParam& param) override
        {
            param.resetToDefault();
        }
    };

    REGISTER_FIELD_EDITOR(ParamType::Float2, Float2FieldEditor)

} // namespace vkBasalt
