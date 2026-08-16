#include "Webserv.hpp"
volatile sig_atomic_t g_loop_running;

// Handles signal.
static void handleSignal(int sig)
{
    if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT)
    {
        g_loop_running = 0;
    }
}

// Sets up signal handlers.
void setupSignalHandlers()
{
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    signal(SIGQUIT, handleSignal);
    signal(SIGPIPE, SIG_IGN);
}
