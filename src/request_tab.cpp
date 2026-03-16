#include "request_tab.hpp"

#include "curl_parser.hpp"
#include <algorithm>
#include <format>
#include <thread>

// ── method colours
// ────────────────────────────────────────────────────────────

wxColour MethodColor(const std::string& method) {
    if (method == "GET")
        return wxColour(0x61, 0xAF, 0xEF); // blue
    if (method == "POST")
        return wxColour(0x98, 0xC3, 0x79); // green
    if (method == "PUT")
        return wxColour(0xE5, 0xC0, 0x7B); // amber
    if (method == "PATCH")
        return wxColour(0xC6, 0x78, 0xDD); // purple
    if (method == "DELETE")
        return wxColour(0xE0, 0x6C, 0x75); // red
    if (method == "HEAD")
        return wxColour(0x56, 0xB6, 0xC2); // teal
    if (method == "OPTIONS")
        return wxColour(0xAB, 0xB2, 0xBF); // grey
    return wxColour(0xAB, 0xB2, 0xBF);
}

static wxColour MethodTextColor(const std::string& method) {
    auto c = MethodColor(method);
    int lum = (c.Red() * 299 + c.Green() * 587 + c.Blue() * 114) / 1000;
    return lum > 145 ? wxColour(30, 30, 30) : *wxWHITE;
}

// ── grid helpers
// ──────────────────────────────────────────────────────────────

wxGrid* RequestTab::MakeKeyValueGrid(wxWindow* parent, bool editable) {
    auto* grid = new wxGrid(parent, wxID_ANY);
    grid->CreateGrid(1, 2);
    grid->SetColLabelValue(0, "Key");
    grid->SetColLabelValue(1, "Value");
    grid->SetRowLabelSize(0);
    grid->SetColLabelSize(22);
    grid->SetDefaultRowSize(24);
    grid->SetColSize(0, 200);
    grid->DisableDragColSize();

    if (!editable)
        grid->EnableEditing(false);

    grid->Bind(wxEVT_SIZE, [grid](wxSizeEvent& evt) {
        int w = evt.GetSize().GetWidth() - 200 - 2;
        if (w > 0)
            grid->SetColSize(1, w);
        evt.Skip();
    });

    if (editable) {
        grid->Bind(wxEVT_GRID_CELL_CHANGED, [this, grid](wxGridEvent& evt) {
            int last = grid->GetNumberRows() - 1;
            if (evt.GetRow() == last) {
                for (int c = 0; c < grid->GetNumberCols(); c++) {
                    if (!grid->GetCellValue(last, c).IsEmpty()) {
                        grid->AppendRows(1);
                        break;
                    }
                }
            }
            MarkDirty();
            evt.Skip();
        });
    }

    return grid;
}

void RequestTab::SetGridRows(wxGrid* grid, const std::map<std::string, std::string>& data) {
    int current = grid->GetNumberRows();
    int needed = static_cast<int>(data.size());
    if (current < needed)
        grid->AppendRows(needed - current);
    else if (current > needed)
        grid->DeleteRows(0, current - needed);
    int row = 0;
    for (auto& [k, v] : data) {
        grid->SetCellValue(row, 0, k);
        grid->SetCellValue(row, 1, v);
        row++;
    }
}

// ── constructor
// ───────────────────────────────────────────────────────────────

RequestTab::RequestTab(wxWindow* parent, const std::string& name, std::shared_ptr<AppGate> gate)
    : wxPanel(parent), m_name(name), m_gate(std::move(gate)) {
    BuildUI();
}

RequestTab::~RequestTab() {
    m_alive->store(false); // signal any in-flight thread not to post back
}

// ── method button
// ─────────────────────────────────────────────────────────────

void RequestTab::SetMethod(const std::string& method) {
    m_currentMethod = method;
    m_methodBtn->SetLabel(method);
    m_methodBtn->SetBackgroundColour(MethodColor(method));
    m_methodBtn->SetForegroundColour(MethodTextColor(method));
    m_methodBtn->Refresh();
    MarkDirty();
}

// ── dirty / tab title
// ─────────────────────────────────────────────────────────

void RequestTab::MarkDirty() {
    if (!m_loading)
        SetDirty(true);
}

void RequestTab::SetDirty(bool dirty) {
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    UpdateTabTitle();
}

void RequestTab::UpdateTabTitle() {
    for (wxWindow* p = GetParent(); p; p = p->GetParent()) {
        if (auto* nb = dynamic_cast<wxAuiNotebook*>(p)) {
            int idx = nb->GetPageIndex(this);
            if (idx != wxNOT_FOUND) {
                nb->SetPageText(idx, m_name + (m_dirty ? " *" : "   "));
                nb->Refresh(); // triggers DearTabArt::DrawTab with updated dirty state
            }
            return;
        }
    }
}

void RequestTab::SetTabName(const std::string& n) {
    m_name = n;
    UpdateTabTitle();
}

// ── UI
// ────────────────────────────────────────────────────────────────────────

void RequestTab::BuildUI() {
    auto* root = new wxBoxSizer(wxVERTICAL);

    // request bar
    auto* bar = new wxPanel(this);
    auto* barSizer = new wxBoxSizer(wxHORIZONTAL);

    m_methodBtn = new wxButton(bar, wxID_ANY, "GET", wxDefaultPosition, wxSize(90, -1));
    SetMethod("GET"); // applies colour without marking dirty

    m_urlInput = new wxTextCtrl(bar, wxID_ANY, "https://", wxDefaultPosition, wxDefaultSize,
                                wxTE_PROCESS_ENTER);

    m_sendButton = new wxButton(bar, wxID_ANY, "Send", wxDefaultPosition, wxSize(70, -1));

    barSizer->Add(m_methodBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    barSizer->Add(m_urlInput, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    barSizer->Add(m_sendButton, 0, wxALIGN_CENTER_VERTICAL);
    bar->SetSizer(barSizer);

    // splitter
    auto* splitter =
        new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);

    // request notebook
    auto* reqPanel = new wxPanel(splitter);
    auto* reqNotebook = new wxNotebook(reqPanel, wxID_ANY);

    m_paramsGrid = MakeKeyValueGrid(reqNotebook, true);
    m_headersGrid = MakeKeyValueGrid(reqNotebook, true);
    reqNotebook->AddPage(m_paramsGrid, "Params");
    reqNotebook->AddPage(m_headersGrid, "Headers");

    auto* bodyPanel = new wxPanel(reqNotebook);
    auto* bodySizer = new wxBoxSizer(wxVERTICAL);

    wxArrayString bodyTypes;
    for (auto& t : {"none", "raw (JSON)", "raw (text)", "form-urlencoded"})
        bodyTypes.Add(t);

    m_bodyTypeChoice =
        new wxChoice(bodyPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, bodyTypes);
    m_bodyTypeChoice->SetSelection(0);

    m_bodyInput = new wxTextCtrl(bodyPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                 wxTE_MULTILINE | wxTE_DONTWRAP | wxHSCROLL);
    m_bodyInput->SetFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE)));

    bodySizer->Add(m_bodyTypeChoice, 0, wxEXPAND | wxBOTTOM, 4);
    bodySizer->Add(m_bodyInput, 1, wxEXPAND);
    bodyPanel->SetSizer(bodySizer);
    reqNotebook->AddPage(bodyPanel, "Body");

    auto* reqSizer = new wxBoxSizer(wxVERTICAL);
    reqSizer->Add(reqNotebook, 1, wxEXPAND);
    reqPanel->SetSizer(reqSizer);

    // response panel
    auto* resPanel = new wxPanel(splitter);
    auto* resSizer = new wxBoxSizer(wxVERTICAL);

    auto* statusPanel = new wxPanel(resPanel);
    auto* statusSizer = new wxBoxSizer(wxHORIZONTAL);
    m_statusLabel = new wxStaticText(statusPanel, wxID_ANY, "Ready");
    statusSizer->Add(m_statusLabel, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    statusPanel->SetSizer(statusSizer);

    auto* resNotebook = new wxNotebook(resPanel, wxID_ANY);

    m_responseBody = new wxTextCtrl(resNotebook, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                    wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP | wxHSCROLL);
    m_responseBody->SetFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE)));
    resNotebook->AddPage(m_responseBody, "Body");

    m_responseHeadersGrid = MakeKeyValueGrid(resNotebook, false);
    resNotebook->AddPage(m_responseHeadersGrid, "Headers");

    resSizer->Add(statusPanel, 0, wxEXPAND | wxTOP | wxBOTTOM, 4);
    resSizer->Add(resNotebook, 1, wxEXPAND);
    resPanel->SetSizer(resSizer);

    splitter->SplitHorizontally(reqPanel, resPanel, 280);
    splitter->SetMinimumPaneSize(80);

    root->Add(bar, 0, wxEXPAND | wxALL, 6);
    root->Add(splitter, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    SetSizer(root);

    // ── method button popup ───────────────────────────────────────────────────
    m_methodBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        const std::vector<std::string> methods = {"GET",    "POST", "PUT",    "PATCH",
                                                  "DELETE", "HEAD", "OPTIONS"};
        wxMenu menu;
        for (int i = 0; i < (int)methods.size(); i++)
            menu.Append(wxID_HIGHEST + 500 + i, methods[i]);
        menu.Bind(wxEVT_MENU, [this, methods](wxCommandEvent& e) {
            int idx = e.GetId() - (wxID_HIGHEST + 500);
            if (idx >= 0 && idx < (int)methods.size())
                SetMethod(methods[idx]);
        });
        PopupMenu(&menu);
    });

    // ── dirty tracking + curl paste detection ────────────────────────────────
    m_urlInput->Bind(wxEVT_TEXT, [this](wxCommandEvent& e) {
        std::string val = m_urlInput->GetValue().ToStdString();
        // check if the pasted value looks like a curl command
        std::string trimmed = val;
        size_t s = trimmed.find_first_not_of(" \t\r\n");
        if (s != std::string::npos) {
            if (trimmed[s] == '$')
                s = trimmed.find_first_not_of(" \t", s + 1);
        }
        if (s != std::string::npos) {
            std::string prefix = trimmed.substr(s, 5);
            std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
            if (prefix == "curl ") {
                HttpRequest parsed = ParseCurl(val);
                if (!parsed.url.empty()) {
                    LoadRequest(parsed);
                    return; // LoadRequest clears dirty; don't skip so field shows url
                }
            }
        }
        MarkDirty();
        e.Skip();
    });
    m_bodyInput->Bind(wxEVT_TEXT, [this](wxCommandEvent& e) {
        MarkDirty();
        e.Skip();
    });
    m_bodyTypeChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent& e) {
        MarkDirty();
        e.Skip();
    });

    // ── Ctrl+S / Cmd+S save ───────────────────────────────────────────────────
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& evt) {
        if (evt.GetKeyCode() == 'S' && (evt.ControlDown() || evt.MetaDown())) {
            if (onSave)
                onSave(BuildCurrentRequest(), m_name);
            return; // don't skip — consume the shortcut
        }
        evt.Skip();
    });

    m_sendButton->Bind(wxEVT_BUTTON, &RequestTab::OnSend, this);
    m_urlInput->Bind(wxEVT_TEXT_ENTER, &RequestTab::OnSend, this);
}

// ── request building / loading
// ────────────────────────────────────────────────

HttpRequest RequestTab::BuildCurrentRequest() const {
    HttpRequest req;
    req.method = m_currentMethod;
    req.url = m_urlInput->GetValue().ToStdString();

    std::string qs;
    for (int r = 0; r < m_paramsGrid->GetNumberRows(); r++) {
        auto key = m_paramsGrid->GetCellValue(r, 0).ToStdString();
        auto val = m_paramsGrid->GetCellValue(r, 1).ToStdString();
        if (!key.empty()) {
            qs += (qs.empty() ? "?" : "&");
            qs += key + "=" + val;
        }
    }
    if (!qs.empty()) {
        auto q = req.url.find('?');
        if (q != std::string::npos)
            req.url = req.url.substr(0, q);
        req.url += qs;
    }

    for (int r = 0; r < m_headersGrid->GetNumberRows(); r++) {
        auto key = m_headersGrid->GetCellValue(r, 0).ToStdString();
        auto val = m_headersGrid->GetCellValue(r, 1).ToStdString();
        if (!key.empty())
            req.headers[key] = val;
    }

    int bodyType = m_bodyTypeChoice->GetSelection();
    if (bodyType != 0) {
        req.body = m_bodyInput->GetValue().ToStdString();
        if (req.headers.find("Content-Type") == req.headers.end()) {
            if (bodyType == 1)
                req.headers["Content-Type"] = "application/json";
            else if (bodyType == 2)
                req.headers["Content-Type"] = "text/plain";
            else if (bodyType == 3)
                req.headers["Content-Type"] = "application/x-www-form-urlencoded";
        }
    }

    return req;
}

HttpRequest RequestTab::GetRequest() const {
    return BuildCurrentRequest();
}

void RequestTab::LoadRequest(const HttpRequest& req) {
    m_loading = true;

    // clear params grid so stale rows don't leak into the next BuildCurrentRequest()
    if (m_paramsGrid->GetNumberRows() > 0)
        m_paramsGrid->DeleteRows(0, m_paramsGrid->GetNumberRows());
    m_paramsGrid->AppendRows(1);

    // SetMethod doesn't call MarkDirty while m_loading, but it still
    // recolours the button — use it directly
    m_currentMethod = req.method;
    m_methodBtn->SetLabel(req.method);
    m_methodBtn->SetBackgroundColour(MethodColor(req.method));
    m_methodBtn->SetForegroundColour(MethodTextColor(req.method));
    m_methodBtn->Refresh();

    m_urlInput->SetValue(req.url);

    int needed = static_cast<int>(req.headers.size());
    int current = m_headersGrid->GetNumberRows();
    int target = needed + 1;
    if (current < target)
        m_headersGrid->AppendRows(target - current);
    else if (current > target)
        m_headersGrid->DeleteRows(0, current - target);

    int row = 0;
    for (auto& [k, v] : req.headers) {
        m_headersGrid->SetCellValue(row, 0, k);
        m_headersGrid->SetCellValue(row, 1, v);
        row++;
    }
    m_headersGrid->SetCellValue(row, 0, "");
    m_headersGrid->SetCellValue(row, 1, "");

    m_bodyInput->SetValue(req.body);
    if (req.body.empty()) {
        m_bodyTypeChoice->SetSelection(0);
    } else {
        auto ct = req.headers.find("Content-Type");
        if (ct != req.headers.end()) {
            if (ct->second.find("application/json") != std::string::npos)
                m_bodyTypeChoice->SetSelection(1);
            else if (ct->second.find("text/") != std::string::npos)
                m_bodyTypeChoice->SetSelection(2);
            else if (ct->second.find("application/x-www-form-urlencoded") != std::string::npos)
                m_bodyTypeChoice->SetSelection(3);
            else
                m_bodyTypeChoice->SetSelection(1);
        } else {
            m_bodyTypeChoice->SetSelection(1);
        }
    }

    m_loading = false;
    SetDirty(false);
}

// ── event handlers
// ────────────────────────────────────────────────────────────

void RequestTab::OnSend(wxCommandEvent&) {
    m_sendButton->Disable();
    m_statusLabel->SetLabel("Sending...");

    // snapshot everything the thread needs — no this access inside the thread body
    HttpRequest req = BuildCurrentRequest();
    auto alive = m_alive;
    auto gate = m_gate;
    RequestTab* self = this;
    std::thread([req, alive, gate, self]() {
        HttpResponse res = sendRequest(req); // pure data, no this
        // gate->post holds AppGate::m_mu while calling CallAfter.
        // AppGate::shutdown() acquires the same mutex before setting alive=false.
        // So it is impossible for this callback to reach CallAfter after the app
        // has started tearing down wxTheApp.
        if (gate)
            gate->post([res, req, alive, self]() {
                if (!alive->load())
                    return;
                self->HandleResponse(res, req);
            });
    }).detach();
}

void RequestTab::HandleResponse(const HttpResponse& res, const HttpRequest& req) {
    m_sendButton->Enable();

    if (onRequestComplete)
        onRequestComplete(req); // exact snapshot of what was sent

    if (!res.success()) {
        m_statusLabel->SetLabel("Error: " + res.error);
        m_statusLabel->SetForegroundColour(*wxRED);
        m_responseBody->SetValue(res.error);
        m_statusLabel->GetParent()->Layout();
        return;
    }

    wxColour color;
    if (res.statusCode >= 500)
        color = wxColour(220, 50, 50);
    else if (res.statusCode >= 400)
        color = wxColour(220, 140, 20);
    else if (res.statusCode >= 300)
        color = wxColour(50, 120, 220);
    else
        color = wxColour(30, 160, 80);

    m_statusLabel->SetLabel(std::format("{} {}   {:.0f} ms   {} bytes", res.statusCode,
                                        res.statusMessage, res.elapsedMs, res.body.size()));
    m_statusLabel->SetForegroundColour(color);
    m_statusLabel->GetParent()->Layout();

    m_responseBody->SetValue(res.body);
    SetGridRows(m_responseHeadersGrid, res.headers);
}
