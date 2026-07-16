#include "reshade_parser.hpp"

#include <algorithm>
#include <set>
#include <signal.h>
#include <setjmp.h>

#include "logger.hpp"
#include "config_serializer.hpp"
#include "shader_cache.hpp"

namespace vkBasalt
{
    namespace
    {
        template<typename T>
        auto findAnnotation(const T& annotations, const std::string& name)
        {
            return std::find_if(annotations.begin(), annotations.end(),
                [&name](const auto& a) { return a.name == name; });
        }

        template<typename T>
        bool hasAnnotation(const T& annotations, const std::string& name)
        {
            return findAnnotation(annotations, name) != annotations.end();
        }

        template<typename T>
        float getAnnotationFloat(const T& annotation)
        {
            return annotation.type.is_floating_point()
                ? annotation.value.as_float[0]
                : static_cast<float>(annotation.value.as_int[0]);
        }

        template<typename T>
        int getAnnotationInt(const T& annotation)
        {
            return annotation.type.is_integral()
                ? annotation.value.as_int[0]
                : static_cast<int>(annotation.value.as_float[0]);
        }

        std::vector<std::string> parseNullSeparatedString(const std::string& str)
        {
            std::vector<std::string> items;
            size_t start = 0;

            for (size_t i = 0; i <= str.size(); i++)
            {
                bool atEnd = (i == str.size() || str[i] == '\0');
                if (!atEnd)
                    continue;

                if (i > start)
                    items.push_back(str.substr(start, i - start));
                start = i + 1;
            }

            return items;
        }

        // Placeholder resolution for parameter/metadata compiles; the real
        // per-swapchain compile in effect_reshade.cpp uses the actual extent.
        std::vector<std::pair<std::string, std::string>> defaultMacros()
        {
            return {
                {"BUFFER_WIDTH", "1920"},
                {"BUFFER_HEIGHT", "1080"},
                {"BUFFER_RCP_WIDTH", "(1.0 / BUFFER_WIDTH)"},
                {"BUFFER_RCP_HEIGHT", "(1.0 / BUFFER_HEIGHT)"},
                {"BUFFER_COLOR_DEPTH", "8"},
                {"BUFFER_COLOR_BIT_DEPTH", "BUFFER_COLOR_DEPTH"},
            };
        }

        void applyFloatRange(FloatParam& p, const auto& annotations)
        {
            auto minIt = findAnnotation(annotations, "ui_min");
            auto maxIt = findAnnotation(annotations, "ui_max");

            if (minIt != annotations.end())
                p.minValue = getAnnotationFloat(*minIt);
            if (maxIt != annotations.end())
                p.maxValue = getAnnotationFloat(*maxIt);
        }

        void applyIntRange(IntParam& p, const auto& annotations)
        {
            auto minIt = findAnnotation(annotations, "ui_min");
            auto maxIt = findAnnotation(annotations, "ui_max");

            if (minIt != annotations.end())
                p.minValue = getAnnotationInt(*minIt);
            if (maxIt != annotations.end())
                p.maxValue = getAnnotationInt(*maxIt);
        }

        std::unique_ptr<EffectParam> convertSpecConstant(
            const reshadefx::uniform_info& spec,
            const std::string& effectName,
            Config* pConfig)
        {
            auto labelIt = findAnnotation(spec.annotations, "ui_label");
            std::string label = (labelIt != spec.annotations.end()) ? labelIt->value.string_data : spec.name;

            auto tooltipIt = findAnnotation(spec.annotations, "ui_tooltip");
            std::string tooltip = (tooltipIt != spec.annotations.end()) ? tooltipIt->value.string_data : "";

            auto typeIt = findAnnotation(spec.annotations, "ui_type");
            std::string uiType = (typeIt != spec.annotations.end()) ? typeIt->value.string_data : "";

            auto populateFloatVector = [&](FloatVecParam& p, uint32_t componentCount) {
                p.effectName = effectName;
                p.name = spec.name;
                p.label = label;
                p.tooltip = tooltip;
                p.uiType = uiType;
                p.componentCount = componentCount;

                auto minIt = findAnnotation(spec.annotations, "ui_min");
                auto maxIt = findAnnotation(spec.annotations, "ui_max");
                for (uint32_t c = 0; c < componentCount; c++)
                {
                    std::string suffix = "[" + std::to_string(c) + "]";
                    p.defaultValue[c] = spec.initializer_value.as_float[c];
                    p.value[c] = pConfig->getInstanceOption<float>(effectName, spec.name + suffix, p.defaultValue[c]);
                    if (minIt != spec.annotations.end())
                        p.minValue[c] = getAnnotationFloat(*minIt);
                    if (maxIt != spec.annotations.end())
                        p.maxValue[c] = getAnnotationFloat(*maxIt);
                }

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p.step = getAnnotationFloat(*stepIt);
            };

            auto populateIntVector = [&](IntVecParam& p, uint32_t componentCount) {
                p.effectName = effectName;
                p.name = spec.name;
                p.label = label;
                p.tooltip = tooltip;
                p.uiType = uiType;
                p.componentCount = componentCount;

                auto minIt = findAnnotation(spec.annotations, "ui_min");
                auto maxIt = findAnnotation(spec.annotations, "ui_max");
                for (uint32_t c = 0; c < componentCount; c++)
                {
                    std::string suffix = "[" + std::to_string(c) + "]";
                    p.defaultValue[c] = spec.initializer_value.as_int[c];
                    p.value[c] = pConfig->getInstanceOption<int32_t>(effectName, spec.name + suffix, p.defaultValue[c]);
                    if (minIt != spec.annotations.end())
                        p.minValue[c] = getAnnotationInt(*minIt);
                    if (maxIt != spec.annotations.end())
                        p.maxValue[c] = getAnnotationInt(*maxIt);
                }

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p.step = getAnnotationFloat(*stepIt);
            };

            auto populateUintVector = [&](UintVecParam& p, uint32_t componentCount) {
                p.effectName = effectName;
                p.name = spec.name;
                p.label = label;
                p.tooltip = tooltip;
                p.uiType = uiType;
                p.componentCount = componentCount;

                auto minIt = findAnnotation(spec.annotations, "ui_min");
                auto maxIt = findAnnotation(spec.annotations, "ui_max");
                for (uint32_t c = 0; c < componentCount; c++)
                {
                    std::string suffix = "[" + std::to_string(c) + "]";
                    p.defaultValue[c] = spec.initializer_value.as_uint[c];
                    p.value[c] = pConfig->getInstanceOption<uint32_t>(effectName, spec.name + suffix, p.defaultValue[c]);
                    if (minIt != spec.annotations.end())
                        p.minValue[c] = static_cast<uint32_t>(getAnnotationInt(*minIt));
                    if (maxIt != spec.annotations.end())
                        p.maxValue[c] = static_cast<uint32_t>(getAnnotationInt(*maxIt));
                }

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p.step = getAnnotationFloat(*stepIt);
            };

            if (spec.type.is_floating_point() && spec.type.rows >= 2 && spec.type.rows <= 4)
            {
                auto p = std::make_unique<FloatVecParam>();
                populateFloatVector(*p, spec.type.rows);
                return p;
            }
            else if (spec.type.is_floating_point() && spec.type.rows == 1)
            {
                auto p = std::make_unique<FloatParam>();
                p->effectName = effectName;
                p->name = spec.name;
                p->label = label;
                p->tooltip = tooltip;
                p->uiType = uiType;
                p->defaultValue = spec.initializer_value.as_float[0];
                p->value = pConfig->getInstanceOption<float>(effectName, spec.name, p->defaultValue);
                applyFloatRange(*p, spec.annotations);

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p->step = getAnnotationFloat(*stepIt);

                return p;
            }
            else if (spec.type.is_boolean())
            {
                auto p = std::make_unique<BoolParam>();
                p->effectName = effectName;
                p->name = spec.name;
                p->label = label;
                p->tooltip = tooltip;
                p->uiType = uiType;
                p->defaultValue = (spec.initializer_value.as_uint[0] != 0);
                p->value = pConfig->getInstanceOption<bool>(effectName, spec.name, p->defaultValue);
                return p;
            }
            else if (spec.type.is_integral() && spec.type.is_signed() && spec.type.rows >= 2 && spec.type.rows <= 4)
            {
                auto p = std::make_unique<IntVecParam>();
                populateIntVector(*p, spec.type.rows);
                return p;
            }
            else if (spec.type.is_integral() && spec.type.is_signed() && spec.type.rows == 1)
            {
                auto p = std::make_unique<IntParam>();
                p->effectName = effectName;
                p->name = spec.name;
                p->label = label;
                p->tooltip = tooltip;
                p->uiType = uiType;
                p->defaultValue = spec.initializer_value.as_int[0];
                p->value = pConfig->getInstanceOption<int32_t>(effectName, spec.name, p->defaultValue);
                applyIntRange(*p, spec.annotations);

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p->step = getAnnotationFloat(*stepIt);

                auto itemsIt = findAnnotation(spec.annotations, "ui_items");
                if (itemsIt != spec.annotations.end())
                    p->items = parseNullSeparatedString(itemsIt->value.string_data);

                return p;
            }
            else if (spec.type.is_integral() && !spec.type.is_signed() && spec.type.rows >= 2 && spec.type.rows <= 4)
            {
                auto p = std::make_unique<UintVecParam>();
                populateUintVector(*p, spec.type.rows);
                return p;
            }
            else if (spec.type.is_integral() && !spec.type.is_signed() && spec.type.rows == 1)
            {
                auto p = std::make_unique<UintParam>();
                p->effectName = effectName;
                p->name = spec.name;
                p->label = label;
                p->tooltip = tooltip;
                p->uiType = uiType;
                p->defaultValue = spec.initializer_value.as_uint[0];
                p->value = pConfig->getInstanceOption<uint32_t>(effectName, spec.name, p->defaultValue);

                auto minIt = findAnnotation(spec.annotations, "ui_min");
                auto maxIt = findAnnotation(spec.annotations, "ui_max");
                if (minIt != spec.annotations.end())
                    p->minValue = static_cast<uint32_t>(getAnnotationInt(*minIt));
                if (maxIt != spec.annotations.end())
                    p->maxValue = static_cast<uint32_t>(getAnnotationInt(*maxIt));

                auto stepIt = findAnnotation(spec.annotations, "ui_step");
                if (stepIt != spec.annotations.end())
                    p->step = getAnnotationFloat(*stepIt);

                return p;
            }

            return nullptr;
        }

        bool shouldSkipSpecConstant(const reshadefx::uniform_info& spec)
        {
            if (spec.name.empty())
                return true;
            if (hasAnnotation(spec.annotations, "source"))
                return true;
            return false;
        }
    } // anonymous namespace

    static thread_local sigjmp_buf parserSignalJmpBuf;
    static thread_local volatile sig_atomic_t parserSignalJmpActive = 0;
    static thread_local volatile sig_atomic_t parserCaughtSignal = 0;

    static void parserCrashHandler(int sig)
    {
        if (parserSignalJmpActive)
        {
            parserCaughtSignal = sig;
            siglongjmp(parserSignalJmpBuf, 1);
        }
        signal(sig, SIG_DFL);
        raise(sig);
    }

    static void installParserCrashHandlers()
    {
        static bool installed = false;
        if (installed)
            return;
        struct sigaction sa = {};
        sa.sa_handler = parserCrashHandler;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGFPE, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        installed = true;
    }

    std::vector<std::unique_ptr<EffectParam>> parseReshadeEffect(
        const std::string& effectName,
        const std::string& effectPath,
        Config* pConfig)
    {
        std::vector<std::unique_ptr<EffectParam>> params;

        installParserCrashHandlers();
        if (sigsetjmp(parserSignalJmpBuf, 1) != 0)
        {
            parserSignalJmpActive = 0;
            Logger::err("parseReshadeEffect: caught signal for " + effectName);
            return params;
        }
        parserSignalJmpActive = 1;

        try
        {

        ShaderManagerConfig smConfig = ConfigSerializer::loadShaderManagerConfig();
        auto compiled = getOrCompileReshadeEffect(effectPath, defaultMacros(), smConfig.discoveredShaderPaths);
        if (!compiled->ok())
        {
            Logger::err("reshade_parser: " + effectPath + ": " + compiled->error);
            parserSignalJmpActive = 0;
            return params;
        }

        const reshadefx::module& module = compiled->module;

        // float2/3/4 arrive as consecutive same-named scalar spec_constants; combine them.
        for (size_t i = 0; i < module.spec_constants.size(); i++)
        {
            const auto& spec = module.spec_constants[i];

            if (shouldSkipSpecConstant(spec))
                continue;

            size_t componentCount = 1;
            while (i + componentCount < module.spec_constants.size() &&
                   module.spec_constants[i + componentCount].name == spec.name)
            {
                componentCount++;
            }

            if (componentCount >= 2 && componentCount <= 4)
            {
                auto labelIt = findAnnotation(spec.annotations, "ui_label");
                std::string label = (labelIt != spec.annotations.end()) ? labelIt->value.string_data : spec.name;

                auto tooltipIt = findAnnotation(spec.annotations, "ui_tooltip");
                std::string tooltip = (tooltipIt != spec.annotations.end()) ? tooltipIt->value.string_data : "";

                auto typeIt = findAnnotation(spec.annotations, "ui_type");
                std::string uiType = (typeIt != spec.annotations.end()) ? typeIt->value.string_data : "";

                auto minIt = findAnnotation(spec.annotations, "ui_min");
                auto maxIt = findAnnotation(spec.annotations, "ui_max");
                auto stepIt = findAnnotation(spec.annotations, "ui_step");

                if (spec.type.is_floating_point())
                {
                    auto p = std::make_unique<FloatVecParam>();
                    p->effectName = effectName;
                    p->name = spec.name;
                    p->label = label;
                    p->tooltip = tooltip;
                    p->uiType = uiType;
                    p->componentCount = static_cast<uint32_t>(componentCount);

                    for (size_t c = 0; c < componentCount; c++)
                    {
                        std::string suffix = "[" + std::to_string(c) + "]";
                        p->defaultValue[c] = module.spec_constants[i + c].initializer_value.as_float[0];
                        p->value[c] = pConfig->getInstanceOption<float>(effectName, spec.name + suffix, p->defaultValue[c]);
                        if (minIt != spec.annotations.end())
                            p->minValue[c] = getAnnotationFloat(*minIt);
                        if (maxIt != spec.annotations.end())
                            p->maxValue[c] = getAnnotationFloat(*maxIt);
                    }
                    if (stepIt != spec.annotations.end())
                        p->step = getAnnotationFloat(*stepIt);

                    params.push_back(std::move(p));
                }
                else if (spec.type.is_integral() && spec.type.is_signed())
                {
                    auto p = std::make_unique<IntVecParam>();
                    p->effectName = effectName;
                    p->name = spec.name;
                    p->label = label;
                    p->tooltip = tooltip;
                    p->uiType = uiType;
                    p->componentCount = static_cast<uint32_t>(componentCount);

                    for (size_t c = 0; c < componentCount; c++)
                    {
                        std::string suffix = "[" + std::to_string(c) + "]";
                        p->defaultValue[c] = module.spec_constants[i + c].initializer_value.as_int[0];
                        p->value[c] = pConfig->getInstanceOption<int32_t>(effectName, spec.name + suffix, p->defaultValue[c]);
                        if (minIt != spec.annotations.end())
                            p->minValue[c] = getAnnotationInt(*minIt);
                        if (maxIt != spec.annotations.end())
                            p->maxValue[c] = getAnnotationInt(*maxIt);
                    }
                    if (stepIt != spec.annotations.end())
                        p->step = getAnnotationFloat(*stepIt);

                    params.push_back(std::move(p));
                }
                else if (spec.type.is_integral() && !spec.type.is_signed())
                {
                    auto p = std::make_unique<UintVecParam>();
                    p->effectName = effectName;
                    p->name = spec.name;
                    p->label = label;
                    p->tooltip = tooltip;
                    p->uiType = uiType;
                    p->componentCount = static_cast<uint32_t>(componentCount);

                    for (size_t c = 0; c < componentCount; c++)
                    {
                        std::string suffix = "[" + std::to_string(c) + "]";
                        p->defaultValue[c] = module.spec_constants[i + c].initializer_value.as_uint[0];
                        p->value[c] = pConfig->getInstanceOption<uint32_t>(effectName, spec.name + suffix, p->defaultValue[c]);
                        if (minIt != spec.annotations.end())
                            p->minValue[c] = static_cast<uint32_t>(getAnnotationInt(*minIt));
                        if (maxIt != spec.annotations.end())
                            p->maxValue[c] = static_cast<uint32_t>(getAnnotationInt(*maxIt));
                    }
                    if (stepIt != spec.annotations.end())
                        p->step = getAnnotationFloat(*stepIt);

                    params.push_back(std::move(p));
                }

                i += componentCount - 1;
            }
            else
            {
                auto param = convertSpecConstant(spec, effectName, pConfig);
                if (param)
                    params.push_back(std::move(param));
            }
        }

        for (const auto& uniform : module.uniforms)
        {
            if (shouldSkipSpecConstant(uniform))
                continue;

            auto param = convertSpecConstant(uniform, effectName, pConfig);
            if (param)
                params.push_back(std::move(param));
        }

        }
        catch (const std::exception& e)
        {
            Logger::err("parseReshadeEffect exception for " + effectName + ": " + e.what());
        }
        catch (...)
        {
            Logger::err("parseReshadeEffect unknown exception for " + effectName);
        }

        parserSignalJmpActive = 0;
        return params;
    }

    ShaderTestResult testShaderCompilation(
        const std::string& effectName,
        const std::string& effectPath)
    {
        ShaderManagerConfig smConfig = ConfigSerializer::loadShaderManagerConfig();
        return testShaderCompilation(effectName, effectPath, smConfig.discoveredShaderPaths);
    }

    ShaderTestResult testShaderCompilation(
        const std::string& effectName,
        const std::string& effectPath,
        const std::vector<std::string>& includePaths)
    {
        ShaderTestResult result;
        result.effectName = effectName;
        result.filePath = effectPath;

        installParserCrashHandlers();
        if (sigsetjmp(parserSignalJmpBuf, 1) != 0)
        {
            parserSignalJmpActive = 0;
            std::string sigName = (parserCaughtSignal == SIGFPE) ? "SIGFPE" : "SIGABRT";
            result.success = false;
            result.errorMessage = sigName + " signal during shader compilation";
            return result;
        }
        parserSignalJmpActive = 1;

        try
        {
            auto compiled = getOrCompileReshadeEffect(effectPath, defaultMacros(), includePaths);
            result.success = compiled->ok();
            result.errorMessage = compiled->ok() ? compiled->warnings : compiled->error;
            result.usesDepth = compiled->usesDepth;
        }
        catch (const std::exception& e)
        {
            result.success = false;
            result.errorMessage = "Exception: " + std::string(e.what());
        }
        catch (...)
        {
            result.success = false;
            result.errorMessage = "Unknown exception during compilation";
        }

        parserSignalJmpActive = 0;
        return result;
    }

    bool checkShaderUsesDepth(
        const std::string& effectName,
        const std::string& effectPath,
        const std::vector<std::string>& includePaths)
    {
        ShaderTestResult result = testShaderCompilation(effectName, effectPath, includePaths);
        return result.usesDepth;
    }

    static const std::set<std::string> builtInMacros = {
        "__RESHADE__",
        "__RESHADE_PERFORMANCE_MODE__",
        "__RENDERER__",
        "BUFFER_WIDTH",
        "BUFFER_HEIGHT",
        "BUFFER_RCP_WIDTH",
        "BUFFER_RCP_HEIGHT",
        "BUFFER_COLOR_DEPTH",
        "__FILE__",
        "__LINE__",
        "__DATE__",
        "__TIME__",
        "__VENDOR__",
        "__APPLICATION__",
        "RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN",
        "RESHADE_DEPTH_INPUT_IS_REVERSED",
        "RESHADE_DEPTH_INPUT_IS_LOGARITHMIC",
        "RESHADE_DEPTH_INPUT_X_SCALE",
        "RESHADE_DEPTH_INPUT_Y_SCALE",
        "RESHADE_DEPTH_INPUT_X_OFFSET",
        "RESHADE_DEPTH_INPUT_Y_OFFSET",
        "RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET",
        "RESHADE_DEPTH_INPUT_Y_PIXEL_OFFSET",
        "RESHADE_DEPTH_LINEARIZATION_FAR_PLANE",
        "RESHADE_DEPTH_MULTIPLIER",
        "RESHADE_MIX_STAGE_DEPTH_MAP",
    };

    std::vector<PreprocessorDefinition> extractPreprocessorDefinitions(
        const std::string& effectName,
        const std::string& effectPath)
    {
        std::vector<PreprocessorDefinition> defs;

        try
        {
            ShaderManagerConfig smConfig = ConfigSerializer::loadShaderManagerConfig();
            auto compiled = getOrCompileReshadeEffect(effectPath, defaultMacros(), smConfig.discoveredShaderPaths);
            if (!compiled->ok())
            {
                Logger::err("extractPreprocessorDefinitions: " + effectPath + ": " + compiled->error);
                return defs;
            }

            for (const auto& [name, value] : compiled->usedMacros)
            {
                if (builtInMacros.count(name))
                    continue;

                if (!name.empty() && name[0] == '_')
                    continue;

                PreprocessorDefinition def;
                def.name = name;
                def.effectName = effectName;
                def.defaultValue = value.empty() ? "1" : value;
                def.value = def.defaultValue;
                defs.push_back(def);
            }

            if (!defs.empty())
            {
                Logger::debug("extractPreprocessorDefinitions: found " + std::to_string(defs.size()) +
                    " user macros in " + effectName);
                for (const auto& def : defs)
                    Logger::debug("  " + def.name + " = " + def.defaultValue);
            }
        }
        catch (const std::exception& e)
        {
            Logger::err("extractPreprocessorDefinitions exception for " + effectName + ": " + e.what());
        }
        catch (...)
        {
            Logger::err("extractPreprocessorDefinitions unknown exception for " + effectName);
        }

        return defs;
    }

} // namespace vkBasalt
