#ifndef RESHADE_UNIFORMS_HPP_INCLUDED
#define RESHADE_UNIFORMS_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <chrono>
#include <memory>

#include "vulkan_include.hpp"
#include "reshade_input_map.hpp"

#include "reshade/effect_module.hpp"

namespace vkBasalt
{
    class EffectRegistry;
    class EffectParam;

    void enumerateReshadeUniforms(reshadefx::effect_module module);

    class ReshadeUniform
    {
    public:
        void virtual update(void* mapedBuffer) = 0;
        virtual bool isParameterUniform() const { return false; }
        virtual ~ReshadeUniform(){};

    protected:
        uint32_t offset;
        uint32_t size;
    };

    std::vector<std::shared_ptr<ReshadeUniform>> createReshadeUniforms(
        reshadefx::effect_module module, EffectRegistry* effectRegistry,
        const std::string& effectName);

    class ParameterUniform : public ReshadeUniform
    {
    public:
        ParameterUniform(reshadefx::uniform uniformInfo,
                         EffectRegistry* effectRegistry,
                         std::string effectName);
        void update(void* mappedBuffer) override;
        bool isParameterUniform() const override { return true; }

    private:
        reshadefx::uniform uniformInfo;
        EffectRegistry* effectRegistry = nullptr;
        std::string effectName;
        uint64_t cachedRegistryRevision = 0;
        const EffectParam* cachedParam = nullptr;
    };

    class FrameTimeUniform : public ReshadeUniform
    {
    public:
        FrameTimeUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        virtual ~FrameTimeUniform();

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> lastFrame;
    };

    class FrameCountUniform : public ReshadeUniform
    {
    public:
        FrameCountUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        virtual ~FrameCountUniform();

    private:
        int32_t count = 0;
    };

    class DateUniform : public ReshadeUniform
    {
    public:
        DateUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        virtual ~DateUniform();
    };

    class TimerUniform : public ReshadeUniform
    {
    public:
        TimerUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        virtual ~TimerUniform();

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> start;
    };

    class PingPongUniform : public ReshadeUniform
    {
    public:
        PingPongUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        virtual ~PingPongUniform();

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> lastFrame;

        float min             = 0.0f;
        float max             = 0.0f;
        float stepMin         = 0.0f;
        float stepMax         = 0.0f;
        float smoothing       = 0.0f;
        float currentValue[2] = {0.0f, 1.0f};
    };

    class RandomUniform : public ReshadeUniform
    {
    public:
        RandomUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        virtual ~RandomUniform();

    private:
        int max = 0;
        int min = 0;
    };

    // Refreshes the shared mouse reading once per frame. Without it every mouse
    // uniform in every effect would poll the display server separately.
    void beginReshadeInputFrame();

    // ReShade's key and mousebutton sources carry a keycode annotation and a
    // mode of "", "press" or "toggle". Press and toggle are edge-triggered, so
    // each uniform keeps the previous frame's reading of its own key.
    class KeyUniform : public ReshadeUniform
    {
    public:
        KeyUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        virtual ~KeyUniform();

    private:
        uint32_t keysym = 0;
        KeyMode  mode   = KeyMode::held;
        bool     wasDown = false;
        bool     toggled = false;
    };

    class MouseButtonUniform : public ReshadeUniform
    {
    public:
        MouseButtonUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        virtual ~MouseButtonUniform();

    private:
        int     button  = 0;
        KeyMode mode    = KeyMode::held;
        bool    wasDown = false;
        bool    toggled = false;
    };

    class MousePointUniform : public ReshadeUniform
    {
    public:
        MousePointUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        virtual ~MousePointUniform();
    };

    class MouseDeltaUniform : public ReshadeUniform
    {
    public:
        MouseDeltaUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        virtual ~MouseDeltaUniform();
    };

    class DepthUniform : public ReshadeUniform
    {
    public:
        DepthUniform(reshadefx::uniform uniformInfo);
        void virtual update(void* mapedBuffer) override;
        void setDepthAvailable(bool available) { depthAvailable = available; }
        virtual ~DepthUniform();

    private:
        bool depthAvailable = false;
    };
} // namespace vkBasalt

#endif // RESHADE_UNIFORMS_HPP_INCLUDED
