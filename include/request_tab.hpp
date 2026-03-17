#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <wx/aui/auibook.h>
#include <wx/grid.h>
#include <wx/notebook.h>
#include <wx/simplebook.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>
#include <wx/wx.h>

#include "app_gate.hpp"
#include "http_client.hpp"

// returns a distinctive colour for each HTTP method
wxColour MethodColor(const std::string& method);

class RequestTab : public wxPanel {
public:
    explicit RequestTab(wxWindow* parent, const std::string& name = "New Request",
                        std::shared_ptr<AppGate> gate = nullptr);
    ~RequestTab();

    void LoadRequest(const HttpRequest& req);
    HttpRequest GetRequest() const;

    std::string GetTabName() const {
        return m_name;
    }
    void SetTabName(const std::string& n);

    int64_t GetSavedId() const {
        return m_savedId;
    }
    void SetSavedId(int64_t id) {
        m_savedId = id;
    }

    bool IsDirty() const {
        return m_dirty;
    }
    void SetDirty(bool dirty);

    // fired after each HTTP response (success or error)
    std::function<void(const HttpRequest&)> onRequestComplete;
    // fired on Ctrl+S; main frame decides save-new vs update
    std::function<void(const HttpRequest&, const std::string&)> onSave;

private:
    std::string m_name;
    int64_t m_savedId{0};
    bool m_dirty{false};
    bool m_loading{false};
    std::shared_ptr<AppGate> m_gate;
    std::shared_ptr<std::atomic<bool>> m_alive{std::make_shared<std::atomic<bool>>(true)};

    wxButton* m_methodBtn{};
    std::string m_currentMethod{"GET"};
    wxTextCtrl* m_urlInput{};
    wxButton* m_sendButton{};
    wxGrid* m_paramsGrid{};
    wxTextCtrl* m_paramsRaw{};
    wxSimplebook* m_paramsBook{};
    wxGrid* m_headersGrid{};
    wxTextCtrl* m_headersRaw{};
    wxSimplebook* m_headersBook{};
    // body
    int m_bodyMode{0}; // 0=none 1=form-data 2=urlencoded 3=raw 4=binary
    wxStaticText* m_bodyTypeLabels[5]{};
    wxSimplebook* m_bodyBook{};
    wxPanel* m_rawExtraPanel{};
    wxChoice* m_rawTypeChoice{};
    wxStyledTextCtrl* m_bodyInput{}; // raw body (STC)
    wxGrid* m_formDataGrid{};
    wxTextCtrl* m_formDataRaw{};
    wxSimplebook* m_formDataBook{};
    wxGrid* m_urlEncodedGrid{};
    wxTextCtrl* m_urlEncodedRaw{};
    wxSimplebook* m_urlEncodedBook{};
    wxTextCtrl* m_binaryPath{};
    wxStaticText* m_statusLabel{};
    wxStyledTextCtrl* m_responseBody{};
    wxGrid* m_responseHeadersGrid{};

    void BuildUI();
    void SetMethod(const std::string& method);
    void SelectBodyMode(int mode);
    void SyncUrlFromParams();
    void MarkDirty();
    void UpdateTabTitle();
    void ApplyBodyLexer(int sel);
    void ApplyResponseLexer(const std::string& ct);

    wxGrid* MakeKeyValueGrid(wxWindow* parent, bool editable);
    wxGrid* MakeFormDataGrid(wxWindow* parent);
    wxPanel* MakeKVPanel(wxWindow* parent, const wxString& title, wxGrid*& gridOut,
                         wxTextCtrl*& rawOut, wxSimplebook*& bookOut);
    void SetGridRows(wxGrid* grid, const std::map<std::string, std::string>& data);
    static std::map<std::string, std::string> GridToMap(wxGrid* grid);
    static std::map<std::string, std::string> ParseRawKV(const wxString& text);
    static wxString MapToRaw(const std::map<std::string, std::string>& m);
    HttpRequest BuildCurrentRequest() const;

    void OnSend(wxCommandEvent&);
    void HandleResponse(const HttpResponse& res, const HttpRequest& req);
};
