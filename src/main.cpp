#include "app_gate.hpp"
#include "main_frame.hpp"
#include <memory>
#include <wx/wx.h>

class DearAPIApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("DearAPI");
        SetAppDisplayName("DearAPI");
        m_gate = std::make_shared<AppGate>();
        auto* frame = new MainFrame(m_gate);
        frame->Show(true);
        return true;
    }

    int OnExit() override {
        m_gate->shutdown(); // no worker can call wxTheApp->CallAfter after this returns
        return wxApp::OnExit();
    }

private:
    std::shared_ptr<AppGate> m_gate;
};

wxIMPLEMENT_APP(DearAPIApp);
