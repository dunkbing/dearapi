#pragma once
#include <functional>
#include <mutex>
#include <wx/wx.h>

// Thread-safe posting gate owned by the application.
// Workers call post() to schedule work on the main thread.
// The app calls shutdown() in OnExit() before wxTheApp becomes invalid.
// post() and shutdown() hold the same mutex, so there is no window where
// a thread can pass the alive check and then call into a dead app.
struct AppGate {
    template <typename F> void post(F&& f) {
        std::lock_guard lock(m_mu);
        if (m_alive)
            wxTheApp->CallAfter(std::forward<F>(f));
    }

    void shutdown() {
        std::lock_guard lock(m_mu);
        m_alive = false;
    }

private:
    std::mutex m_mu;
    bool m_alive{true};
};
