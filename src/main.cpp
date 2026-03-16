#include "main_frame.hpp"
#include <wx/wx.h>

class DearAPIApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("DearAPI");
        SetAppDisplayName("DearAPI");
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(DearAPIApp);
