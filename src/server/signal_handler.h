#pragma once

#include <functional>
#include <csignal>
#include <atomic>

namespace pokemon_game
{

    class SignalHandler
    {
    public:
        static SignalHandler &GetInstance();

        // Register signal handler for SIGTERM
        void RegisterSigTermHandler(std::function<void()> handler);

        // Check if SIGTERM received
        bool IsSigTermReceived() const
        {
            return sig_term_received_;
        }

        // Set the received flag
        static void SetSigTermReceived();

        // Run handler if signal was received; returns true if it ran
        bool RunHandlerIfReceived();

    private:
        SignalHandler();
        ~SignalHandler();

        std::function<void()> sigterm_handler_;
        static std::atomic<bool> sig_term_received_;
        static SignalHandler *instance_;

        static void SignalHandlerFunction(int signal);
    };

} // namespace pokemon_game
