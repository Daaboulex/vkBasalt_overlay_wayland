#ifndef EFFECT_PARAM_HPP_INCLUDED
#define EFFECT_PARAM_HPP_INCLUDED

#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace vkBasalt
{
    enum class ParamType
    {
        Float,
        Float2,
        Float3,
        Float4,
        Int,
        Bool
    };

    // Base class for effect parameters
    class EffectParam
    {
    public:
        virtual ~EffectParam() = default;

        std::string effectName;  // Which effect this belongs to (e.g., "cas", "Clarity.fx")
        std::string name;        // Parameter name (e.g., "casSharpness")
        std::string label;       // Display label (from ui_label or name)
        std::string tooltip;     // ui_tooltip - hover description
        std::string uiType;      // ui_type - "slider", "drag", "combo", etc.

        virtual ParamType getType() const = 0;
        virtual const char* getTypeName() const = 0;
        virtual bool hasChanged() const = 0;
        virtual void resetToDefault() = 0;
        virtual std::vector<std::pair<std::string, std::string>> serialize() const = 0;
        virtual std::unique_ptr<EffectParam> clone() const = 0;
    };

    // Float parameter
    class FloatParam : public EffectParam
    {
    public:
        float value = 0.0f;
        float defaultValue = 0.0f;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float step = 0.0f;

        ParamType getType() const override { return ParamType::Float; }
        const char* getTypeName() const override { return "FLOAT"; }

        bool hasChanged() const override
        {
            return value != defaultValue;
        }

        void resetToDefault() override
        {
            value = defaultValue;
        }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            return {{"", std::to_string(value)}};
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<FloatParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->value = value;
            p->defaultValue = defaultValue;
            p->minValue = minValue;
            p->maxValue = maxValue;
            p->step = step;
            return p;
        }
    };

    // Float2 parameter (vec2)
    class Float2Param : public EffectParam
    {
    public:
        float value[2] = {0.0f, 0.0f};
        float defaultValue[2] = {0.0f, 0.0f};
        float minValue[2] = {0.0f, 0.0f};
        float maxValue[2] = {1.0f, 1.0f};
        float step = 0.0f;

        ParamType getType() const override { return ParamType::Float2; }
        const char* getTypeName() const override { return "FLOAT2"; }

        bool hasChanged() const override
        {
            return value[0] != defaultValue[0] || value[1] != defaultValue[1];
        }

        void resetToDefault() override
        {
            value[0] = defaultValue[0];
            value[1] = defaultValue[1];
        }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            return {
                {name + ".x", std::to_string(value[0])},
                {name + ".y", std::to_string(value[1])}
            };
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<Float2Param>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->value[0] = value[0];
            p->value[1] = value[1];
            p->defaultValue[0] = defaultValue[0];
            p->defaultValue[1] = defaultValue[1];
            p->minValue[0] = minValue[0];
            p->minValue[1] = minValue[1];
            p->maxValue[0] = maxValue[0];
            p->maxValue[1] = maxValue[1];
            p->step = step;
            return p;
        }
    };

    // Float3 parameter (vec3)
    class Float3Param : public EffectParam
    {
    public:
        float value[3] = {0.0f, 0.0f, 0.0f};
        float defaultValue[3] = {0.0f, 0.0f, 0.0f};
        float minValue[3] = {0.0f, 0.0f, 0.0f};
        float maxValue[3] = {1.0f, 1.0f, 1.0f};
        float step = 0.0f;

        ParamType getType() const override { return ParamType::Float3; }
        const char* getTypeName() const override { return "FLOAT3"; }

        bool hasChanged() const override
        {
            return value[0] != defaultValue[0] || value[1] != defaultValue[1] || value[2] != defaultValue[2];
        }

        void resetToDefault() override
        {
            for (int i = 0; i < 3; i++)
                value[i] = defaultValue[i];
        }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            return {
                {name + ".x", std::to_string(value[0])},
                {name + ".y", std::to_string(value[1])},
                {name + ".z", std::to_string(value[2])}
            };
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<Float3Param>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            for (int i = 0; i < 3; i++)
            {
                p->value[i] = value[i];
                p->defaultValue[i] = defaultValue[i];
                p->minValue[i] = minValue[i];
                p->maxValue[i] = maxValue[i];
            }
            p->step = step;
            return p;
        }
    };

    // Float4 parameter (vec4)
    class Float4Param : public EffectParam
    {
    public:
        float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float defaultValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float minValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float maxValue[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float step = 0.0f;

        ParamType getType() const override { return ParamType::Float4; }
        const char* getTypeName() const override { return "FLOAT4"; }

        bool hasChanged() const override
        {
            for (int i = 0; i < 4; i++)
                if (value[i] != defaultValue[i])
                    return true;
            return false;
        }

        void resetToDefault() override
        {
            for (int i = 0; i < 4; i++)
                value[i] = defaultValue[i];
        }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            return {
                {name + ".x", std::to_string(value[0])},
                {name + ".y", std::to_string(value[1])},
                {name + ".z", std::to_string(value[2])},
                {name + ".w", std::to_string(value[3])}
            };
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<Float4Param>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            for (int i = 0; i < 4; i++)
            {
                p->value[i] = value[i];
                p->defaultValue[i] = defaultValue[i];
                p->minValue[i] = minValue[i];
                p->maxValue[i] = maxValue[i];
            }
            p->step = step;
            return p;
        }
    };

    // Int parameter
    class IntParam : public EffectParam
    {
    public:
        int value = 0;
        int defaultValue = 0;
        int minValue = 0;
        int maxValue = 100;
        float step = 0.0f;
        std::vector<std::string> items;  // ui_items - combo box options

        ParamType getType() const override { return ParamType::Int; }
        const char* getTypeName() const override { return "INT"; }

        bool hasChanged() const override
        {
            return value != defaultValue;
        }

        void resetToDefault() override
        {
            value = defaultValue;
        }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            return {{"", std::to_string(value)}};
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<IntParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->value = value;
            p->defaultValue = defaultValue;
            p->minValue = minValue;
            p->maxValue = maxValue;
            p->step = step;
            p->items = items;
            return p;
        }
    };

    // Bool parameter
    class BoolParam : public EffectParam
    {
    public:
        bool value = false;
        bool defaultValue = false;

        ParamType getType() const override { return ParamType::Bool; }
        const char* getTypeName() const override { return "BOOL"; }

        bool hasChanged() const override
        {
            return value != defaultValue;
        }

        void resetToDefault() override
        {
            value = defaultValue;
        }

        std::vector<std::pair<std::string, std::string>> serialize() const override
        {
            return {{"", value ? "true" : "false"}};
        }

        std::unique_ptr<EffectParam> clone() const override
        {
            auto p = std::make_unique<BoolParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->tooltip = tooltip;
            p->uiType = uiType;
            p->value = value;
            p->defaultValue = defaultValue;
            return p;
        }
    };

    // Helper to clone a vector of params
    inline std::vector<std::unique_ptr<EffectParam>> cloneParams(const std::vector<std::unique_ptr<EffectParam>>& params)
    {
        std::vector<std::unique_ptr<EffectParam>> result;
        result.reserve(params.size());
        for (const auto& p : params)
            result.push_back(p->clone());
        return result;
    }

} // namespace vkBasalt

#endif // EFFECT_PARAM_HPP_INCLUDED
