#pragma once
#include <functional>
#include <map>
#include <string>
#include <wx/aui/auibook.h>
#include <wx/grid.h>
#include <wx/notebook.h>
#include <wx/splitter.h>
#include <wx/wx.h>

#include "http_client.hpp"

wxDECLARE_EVENT(EVT_HTTP_RESPONSE, wxThreadEvent);

// returns a distinctive colour for each HTTP method
wxColour MethodColor(const std::string& method);

class RequestTab : public wxPanel {
public:
    explicit RequestTab(wxWindow* parent, const std::string& name = "New Request");

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

    wxButton* m_methodBtn{};
    std::string m_currentMethod{"GET"};
    wxTextCtrl* m_urlInput{};
    wxButton* m_sendButton{};
    wxGrid* m_paramsGrid{};
    wxGrid* m_headersGrid{};
    wxChoice* m_bodyTypeChoice{};
    wxTextCtrl* m_bodyInput{};
    wxStaticText* m_statusLabel{};
    wxTextCtrl* m_responseBody{};
    wxGrid* m_responseHeadersGrid{};

    void BuildUI();
    void SetMethod(const std::string& method);
    void MarkDirty();
    void UpdateTabTitle();

    wxGrid* MakeKeyValueGrid(wxWindow* parent, bool editable);
    void SetGridRows(wxGrid* grid, const std::map<std::string, std::string>& data);
    HttpRequest BuildCurrentRequest() const;

    void OnSend(wxCommandEvent&);
    void OnResponse(wxThreadEvent&);
};
