#ifndef WAYLAND_POINTER_STATE_HPP_INCLUDED
#define WAYLAND_POINTER_STATE_HPP_INCLUDED

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace vkBasalt
{
    struct PointerSample
    {
        int   x      = 0;
        int   y      = 0;
        bool  left   = false;
        bool  right  = false;
        bool  middle = false;
        float scroll = 0.0f;
    };

    class WaylandPointerState
    {
    public:
        using Clock = std::chrono::steady_clock;

        static constexpr uint32_t BTN_LEFT   = 0x110;
        static constexpr uint32_t BTN_RIGHT  = 0x111;
        static constexpr uint32_t BTN_MIDDLE = 0x112;

        static constexpr std::chrono::milliseconds AUTO_RELEASE{200};

        void setBounds(int width, int height)
        {
            boundsWidth  = width;
            boundsHeight = height;
        }

        void absoluteMotion(double x, double y)
        {
            cursorX         = x;
            cursorY         = y;
            absoluteInGroup = true;
            moved           = true;
        }

        void relativeMotion(double dx, double dy)
        {
            pendingDx += dx;
            pendingDy += dy;
        }

        void frame()
        {
            applyPendingRelative();
            discreteScrollInGroup = false;
        }

        void button(uint32_t code, bool pressed, Clock::time_point now)
        {
            Button* b = buttonFor(code);
            if (!b)
                return;

            b->held = pressed;
            if (pressed)
            {
                b->latched   = true;
                b->pressTime = now;
            }
        }

        void scrollDiscrete(double steps)
        {
            discreteScrollInGroup = true;
            scrollAccumulator -= steps;
        }

        void scrollContinuous(double value)
        {
            if (discreteScrollInGroup)
                return;
            scrollAccumulator -= value / 10.0;
        }

        PointerSample sample(Clock::time_point now)
        {
            applyPendingRelative();

            if (moved)
            {
                lastMotion = now;
                moved      = false;
            }
            const bool idle = now - lastMotion > AUTO_RELEASE;

            PointerSample s;
            s.x      = static_cast<int>(cursorX);
            s.y      = static_cast<int>(cursorY);
            s.left   = take(left, now, idle);
            s.right  = take(right, now, idle);
            s.middle = take(middle, now, idle);
            s.scroll = static_cast<float>(scrollAccumulator);

            scrollAccumulator = 0.0;
            return s;
        }

    private:
        struct Button
        {
            bool              held    = false;
            bool              latched = false;
            Clock::time_point pressTime{};
        };

        Button* buttonFor(uint32_t code)
        {
            switch (code)
            {
                case BTN_LEFT: return &left;
                case BTN_RIGHT: return &right;
                case BTN_MIDDLE: return &middle;
            }
            return nullptr;
        }

        static bool take(Button& b, Clock::time_point now, bool idle)
        {
            if (b.held && idle && now - b.pressTime > AUTO_RELEASE)
                b.held = false;

            const bool down = b.held || b.latched;
            b.latched       = false;
            return down;
        }

        void applyPendingRelative()
        {
            if (!absoluteInGroup && (pendingDx != 0.0 || pendingDy != 0.0))
            {
                cursorX = clampTo(cursorX + pendingDx, boundsWidth);
                cursorY = clampTo(cursorY + pendingDy, boundsHeight);
                moved   = true;
            }
            pendingDx       = 0.0;
            pendingDy       = 0.0;
            absoluteInGroup = false;
        }

        static double clampTo(double value, int extent)
        {
            if (extent <= 0)
                return value;
            return std::clamp(value, 0.0, static_cast<double>(extent - 1));
        }

        double cursorX = 0.0;
        double cursorY = 0.0;
        int    boundsWidth  = 0;
        int    boundsHeight = 0;

        double pendingDx       = 0.0;
        double pendingDy       = 0.0;
        bool   absoluteInGroup = false;

        bool              moved = false;
        Clock::time_point lastMotion{};

        Button left;
        Button right;
        Button middle;

        double scrollAccumulator     = 0.0;
        bool   discreteScrollInGroup = false;
    };

} // namespace vkBasalt

#endif // WAYLAND_POINTER_STATE_HPP_INCLUDED
