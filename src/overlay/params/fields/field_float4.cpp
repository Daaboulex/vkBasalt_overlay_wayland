#include "../field_editor.hpp"
#include "../../../imgui/imgui.h"
#include "../../../imgui/imgui_internal.h"
#include <cmath>

namespace vkBasalt
{
    class Float4FieldEditor : public FieldEditor
    {
    public:
        bool render(EffectParam& param) override
        {
            auto& p = static_cast<Float4Param&>(param);
            bool changed = false;

            if (ImGui::SliderFloat4(p.label.c_str(), p.value, p.minValue[0], p.maxValue[0]))
            {
                if (p.step > 0.0f)
                {
                    for (int i = 0; i < 4; i++)
                        p.value[i] = std::round(p.value[i] / p.step) * p.step;
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

    REGISTER_FIELD_EDITOR(ParamType::Float4, Float4FieldEditor)

} // namespace vkBasalt
