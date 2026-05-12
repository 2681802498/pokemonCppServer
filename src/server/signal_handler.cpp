#include "signal_handler.h"
#include <iostream>

namespace pokemon_game
{

    std::atomic<bool> SignalHandler::sig_term_received_(false);
    SignalHandler *SignalHandler::instance_ = nullptr;

    SignalHandler::SignalHandler() {}

    SignalHandler::~SignalHandler() {}

    SignalHandler &SignalHandler::GetInstance()
    {
        static SignalHandler instance;
        return instance;
    }

    void SignalHandler::RegisterSigTermHandler(std::function<void()> handler)
    {
        sigterm_handler_ = handler;
        std::signal(SIGTERM, SignalHandlerFunction);
        std::cout << "SIGTERM handler registered" << std::endl;
    }

    void SignalHandler::SetSigTermReceived()
    {
        sig_term_received_ = true;
    }

    bool SignalHandler::RunHandlerIfReceived()
    {
        if (!sig_term_received_.exchange(false))
        {
            return false;
        }

        SignalHandler &handler = GetInstance();
        if (handler.sigterm_handler_)
        {
            handler.sigterm_handler_();
        }

        return true;
    }

    void SignalHandler::SignalHandlerFunction(int signal)
    {
        if (signal == SIGTERM)
        {
            SetSigTermReceived();
        }
    }

} // namespace pokemon_game
