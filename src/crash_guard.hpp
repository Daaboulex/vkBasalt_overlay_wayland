#pragma once

#include <signal.h>
#include <setjmp.h>

// The reshadefx compiler can raise SIGFPE/SIGABRT, which C++ try-catch cannot
// catch. A guarded region longjmps back; any other hit chains to the previous
// handler, so the application's own crash reporting still runs.
namespace vkBasalt
{
    inline thread_local sigjmp_buf crashJmpBuf;
    inline thread_local volatile sig_atomic_t crashJmpActive   = 0;
    inline thread_local volatile sig_atomic_t crashCaughtSignal = 0;

    namespace detail
    {
        inline struct sigaction crashPreviousFpe;
        inline struct sigaction crashPreviousAbrt;

        inline void crashSignalHandler(int sig)
        {
            if (crashJmpActive)
            {
                crashCaughtSignal = sig;
                siglongjmp(crashJmpBuf, 1);
            }
            sigaction(sig, (sig == SIGFPE) ? &crashPreviousFpe : &crashPreviousAbrt, nullptr);
            raise(sig);
        }
    } // namespace detail

    inline void installCrashHandlers()
    {
        static bool installed = false;
        if (installed)
            return;
        struct sigaction sa = {};
        sa.sa_handler = detail::crashSignalHandler;
        sa.sa_flags   = 0;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGFPE, &sa, &detail::crashPreviousFpe);
        sigaction(SIGABRT, &sa, &detail::crashPreviousAbrt);
        installed = true;
    }
} // namespace vkBasalt
