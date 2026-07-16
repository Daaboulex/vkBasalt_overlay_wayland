#ifndef FIELD_EDITOR_HPP_INCLUDED
#define FIELD_EDITOR_HPP_INCLUDED

#include "effects/params/effect_param.hpp"
#include <memory>
#include <map>
#include <functional>

namespace vkBasalt
{
    class FieldEditor
    {
    public:
        virtual ~FieldEditor() = default;

        virtual bool render(EffectParam& param) = 0;

        virtual void resetToDefault(EffectParam& param) = 0;
    };

    class FieldEditorFactory
    {
    public:
        using CreatorFunc = std::function<std::unique_ptr<FieldEditor>()>;

        static FieldEditorFactory& instance();

        void registerEditor(ParamType type, CreatorFunc creator);

        FieldEditor* getEditor(ParamType type);

    private:
        FieldEditorFactory() = default;
        std::map<ParamType, std::unique_ptr<FieldEditor>> editors;
        std::map<ParamType, CreatorFunc> creators;
    };

    #define REGISTER_FIELD_EDITOR(ParamTypeValue, EditorClass) \
        namespace { \
            static bool _registered_##EditorClass = []() { \
                FieldEditorFactory::instance().registerEditor( \
                    ParamTypeValue, \
                    []() { return std::make_unique<EditorClass>(); }); \
                return true; \
            }(); \
        }

    bool renderFieldEditor(EffectParam& param);

} // namespace vkBasalt

#endif // FIELD_EDITOR_HPP_INCLUDED
