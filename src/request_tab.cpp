#include "request_tab.hpp"

#include "curl_parser.hpp"
#include <algorithm>
#include <format>
#include <fstream>
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

static std::string StripQueryString(const std::string& url) {
    auto q = url.find('?');
    return (q != std::string::npos) ? url.substr(0, q) : url;
}

static std::string BuildQueryString(const std::map<std::string, std::string>& params) {
    std::string qs;
    for (auto& [k, v] : params) {
        qs += (qs.empty() ? "?" : "&");
        qs += k + "=" + v;
    }
    return qs;
}

static void BindColLabelPaint(wxGrid* grid) {
    auto* colWin = grid->GetGridColLabelWindow();
    colWin->Bind(wxEVT_PAINT, [grid, colWin](wxPaintEvent&) {
        wxPaintDC dc(colWin);
        wxSize sz = colWin->GetClientSize();
        dc.SetBrush(wxBrush(grid->GetLabelBackgroundColour()));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(wxRect(sz));
        dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
        dc.DrawLine(0, sz.y - 1, sz.x, sz.y - 1);
        dc.SetFont(grid->GetLabelFont());
        dc.SetTextForeground(grid->GetLabelTextColour());
        int x = 0;
        for (int col = 0; col < grid->GetNumberCols(); col++) {
            int w = grid->GetColSize(col);
            wxString label = grid->GetColLabelValue(col);
            if (!label.empty())
                dc.DrawLabel(label, wxRect(x + 3, 0, w - 6, sz.y - 1),
                             wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
            x += w;
        }
    });
}

// ── STC helpers
// ─────────────────────────────────────────────────────────────────

static void SetupStc(wxStyledTextCtrl* stc, bool readOnly) {
    stc->StyleSetForeground(wxSTC_STYLE_DEFAULT, wxColour(0xAB, 0xB2, 0xBF));
    stc->StyleSetBackground(wxSTC_STYLE_DEFAULT, wxColour(0x28, 0x2C, 0x34));
    stc->StyleClearAll();
    stc->SetCaretForeground(wxColour(0xAB, 0xB2, 0xBF));
    stc->SetFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE)));
    stc->SetWrapMode(wxSTC_WRAP_NONE);
    stc->SetScrollWidth(1);
    stc->SetScrollWidthTracking(true);
    stc->SetTabWidth(2);
    stc->SetUseTabs(false);
    stc->SetMarginWidth(0, 0);
    stc->SetMarginWidth(1, 0);
    if (readOnly)
        stc->SetReadOnly(true);
}

static void ApplyStcLexer(wxStyledTextCtrl* stc, int lexer) {
    stc->SetLexer(lexer);
    stc->StyleClearAll();
    if (lexer == wxSTC_LEX_JSON) {
        stc->StyleSetForeground(wxSTC_JSON_NUMBER, wxColour(0xD1, 0x9A, 0x66));
        stc->StyleSetForeground(wxSTC_JSON_STRING, wxColour(0x98, 0xC3, 0x79));
        stc->StyleSetForeground(wxSTC_JSON_STRINGEOL, wxColour(0xE0, 0x6C, 0x75));
        stc->StyleSetForeground(wxSTC_JSON_PROPERTYNAME, wxColour(0xE0, 0x6C, 0x75));
        stc->StyleSetForeground(wxSTC_JSON_ESCAPESEQUENCE, wxColour(0x56, 0xB6, 0xC2));
        stc->StyleSetForeground(wxSTC_JSON_LINECOMMENT, wxColour(0x5C, 0x63, 0x70));
        stc->StyleSetForeground(wxSTC_JSON_BLOCKCOMMENT, wxColour(0x5C, 0x63, 0x70));
        stc->StyleSetForeground(wxSTC_JSON_OPERATOR, wxColour(0xAB, 0xB2, 0xBF));
        stc->StyleSetForeground(wxSTC_JSON_URI, wxColour(0x61, 0xAF, 0xEF));
        stc->StyleSetForeground(wxSTC_JSON_KEYWORD, wxColour(0xC6, 0x78, 0xDD));
    } else if (lexer == wxSTC_LEX_HTML) {
        stc->StyleSetForeground(wxSTC_H_TAG, wxColour(0xE0, 0x6C, 0x75));
        stc->StyleSetForeground(wxSTC_H_TAGUNKNOWN, wxColour(0xE0, 0x6C, 0x75));
        stc->StyleSetForeground(wxSTC_H_ATTRIBUTE, wxColour(0xD1, 0x9A, 0x66));
        stc->StyleSetForeground(wxSTC_H_ATTRIBUTEUNKNOWN, wxColour(0xD1, 0x9A, 0x66));
        stc->StyleSetForeground(wxSTC_H_NUMBER, wxColour(0xD1, 0x9A, 0x66));
        stc->StyleSetForeground(wxSTC_H_DOUBLESTRING, wxColour(0x98, 0xC3, 0x79));
        stc->StyleSetForeground(wxSTC_H_SINGLESTRING, wxColour(0x98, 0xC3, 0x79));
        stc->StyleSetForeground(wxSTC_H_COMMENT, wxColour(0x5C, 0x63, 0x70));
        stc->StyleSetForeground(wxSTC_H_ENTITY, wxColour(0xC6, 0x78, 0xDD));
    } else if (lexer == wxSTC_LEX_XML) {
        stc->StyleSetForeground(wxSTC_H_TAG, wxColour(0xE0, 0x6C, 0x75));
        stc->StyleSetForeground(wxSTC_H_TAGUNKNOWN, wxColour(0xE0, 0x6C, 0x75));
        stc->StyleSetForeground(wxSTC_H_ATTRIBUTE, wxColour(0xD1, 0x9A, 0x66));
        stc->StyleSetForeground(wxSTC_H_ATTRIBUTEUNKNOWN, wxColour(0xD1, 0x9A, 0x66));
        stc->StyleSetForeground(wxSTC_H_DOUBLESTRING, wxColour(0x98, 0xC3, 0x79));
        stc->StyleSetForeground(wxSTC_H_SINGLESTRING, wxColour(0x98, 0xC3, 0x79));
        stc->StyleSetForeground(wxSTC_H_COMMENT, wxColour(0x5C, 0x63, 0x70));
    } else if (lexer == wxSTC_LEX_CPP) { // JavaScript
        stc->StyleSetForeground(wxSTC_C_COMMENT, wxColour(0x5C, 0x63, 0x70));
        stc->StyleSetForeground(wxSTC_C_COMMENTLINE, wxColour(0x5C, 0x63, 0x70));
        stc->StyleSetForeground(wxSTC_C_COMMENTDOC, wxColour(0x5C, 0x63, 0x70));
        stc->StyleSetForeground(wxSTC_C_NUMBER, wxColour(0xD1, 0x9A, 0x66));
        stc->StyleSetForeground(wxSTC_C_WORD, wxColour(0xC6, 0x78, 0xDD));
        stc->StyleSetForeground(wxSTC_C_STRING, wxColour(0x98, 0xC3, 0x79));
        stc->StyleSetForeground(wxSTC_C_CHARACTER, wxColour(0x98, 0xC3, 0x79));
        stc->StyleSetForeground(wxSTC_C_OPERATOR, wxColour(0xAB, 0xB2, 0xBF));
        stc->StyleSetForeground(wxSTC_C_STRINGEOL, wxColour(0xE0, 0x6C, 0x75));
        stc->SetKeyWords(
            0, "break case catch class const continue debugger default delete do else "
               "export extends finally for function if import in instanceof let new "
               "return static super switch this throw try typeof var void while with yield");
    }
}

// renders grey placeholder text when a cell is empty
class PlaceholderRenderer : public wxGridCellStringRenderer {
public:
    explicit PlaceholderRenderer(const wxString& hint) : m_hint(hint) {}
    void Draw(wxGrid& grid, wxGridCellAttr& attr, wxDC& dc, const wxRect& rect, int row, int col,
              bool isSelected) override {
        if (grid.GetCellValue(row, col).empty() && !isSelected) {
            dc.SetBrush(wxBrush(attr.GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(rect);
            dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            dc.SetFont(attr.GetFont());
            wxRect r = rect;
            r.Deflate(3, 0);
            dc.DrawLabel(m_hint, r, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        } else {
            wxGridCellStringRenderer::Draw(grid, attr, dc, rect, row, col, isSelected);
        }
    }
    wxGridCellRenderer* Clone() const override {
        return new PlaceholderRenderer(m_hint);
    }

private:
    wxString m_hint;
};

wxGrid* RequestTab::MakeKeyValueGrid(wxWindow* parent, bool editable) {
    auto* grid = new wxGrid(parent, wxID_ANY);
    grid->SetRowLabelSize(0);
    grid->SetDefaultRowSize(26);
    grid->DisableDragColSize();

    if (editable) {
        // 4 columns: [enabled] Key  Value  Description
        grid->CreateGrid(1, 4);
        grid->SetColLabelSize(24);
        grid->SetColLabelValue(0, "");
        grid->SetColLabelValue(1, "Key");
        grid->SetColLabelValue(2, "Value");
        grid->SetColLabelValue(3, "Description");
        grid->SetColSize(0, 28);
        grid->SetColSize(1, 180);
        grid->SetColSize(3, 150);

        // checkbox column
        auto* boolAttr = new wxGridCellAttr();
        boolAttr->SetRenderer(new wxGridCellBoolRenderer());
        boolAttr->SetEditor(new wxGridCellBoolEditor());
        boolAttr->SetAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
        grid->SetColAttr(0, boolAttr);

        // placeholder renderers
        auto* keyAttr = new wxGridCellAttr();
        keyAttr->SetRenderer(new PlaceholderRenderer("Key"));
        grid->SetColAttr(1, keyAttr);
        auto* valAttr = new wxGridCellAttr();
        valAttr->SetRenderer(new PlaceholderRenderer("Value"));
        grid->SetColAttr(2, valAttr);
        auto* descAttr = new wxGridCellAttr();
        descAttr->SetRenderer(new PlaceholderRenderer("Description"));
        grid->SetColAttr(3, descAttr);

        // initial row: enabled
        grid->SetCellValue(0, 0, "1");

        grid->SetSelectionMode(wxGrid::wxGridSelectCells);
        grid->UseNativeColHeader(false);

        BindColLabelPaint(grid);

        grid->Bind(wxEVT_GRID_TABBING, [this, grid](wxGridEvent& evt) {
            int lastCol = grid->GetNumberCols() - 1;
            int row = evt.GetRow(), col = evt.GetCol();
            if (!evt.ShiftDown() && col == lastCol) {
                // Tab from last col → col 1 of next row
                evt.Veto();
                grid->DisableCellEditControl();
                int nextRow = row + 1;
                if (nextRow >= grid->GetNumberRows()) {
                    grid->AppendRows(1);
                    grid->SetCellValue(nextRow, 0, "1");
                }
                grid->SetGridCursor(nextRow, 1);
                grid->MakeCellVisible(nextRow, 1);
                grid->EnableCellEditControl();
            } else if (evt.ShiftDown() && col <= 1 && row > 0) {
                // Shift+Tab from first editable col → last col of previous row
                evt.Veto();
                grid->DisableCellEditControl();
                grid->SetGridCursor(row - 1, lastCol);
                grid->MakeCellVisible(row - 1, lastCol);
                grid->EnableCellEditControl();
            } else {
                evt.Skip();
            }
        });

        grid->Bind(wxEVT_SIZE, [grid](wxSizeEvent& evt) {
            int w = evt.GetSize().GetWidth() - 28 - 180 - 150 - 4;
            if (w > 40)
                grid->SetColSize(2, w);
            evt.Skip();
        });
        grid->Bind(wxEVT_GRID_CELL_CHANGED, [this, grid](wxGridEvent& evt) {
            int last = grid->GetNumberRows() - 1;
            int col = evt.GetCol();
            if (evt.GetRow() == last && (col == 1 || col == 2)) {
                if (!grid->GetCellValue(last, 1).IsEmpty() ||
                    !grid->GetCellValue(last, 2).IsEmpty()) {
                    grid->AppendRows(1);
                    grid->SetCellValue(grid->GetNumberRows() - 1, 0, "1");
                }
            }
            MarkDirty();
            evt.Skip();
        });
    } else {
        // 2 columns: Key  Value (read-only response grid)
        grid->CreateGrid(1, 2);
        grid->SetColLabelSize(0);
        grid->SetColSize(0, 200);
        grid->EnableEditing(false);

        grid->Bind(wxEVT_SIZE, [grid](wxSizeEvent& evt) {
            int w = evt.GetSize().GetWidth() - 200 - 2;
            if (w > 0)
                grid->SetColSize(1, w);
            evt.Skip();
        });
    }

    return grid;
}

void RequestTab::SetGridRows(wxGrid* grid, const std::map<std::string, std::string>& data) {
    bool has5 = grid->GetNumberCols() >= 5; // form-data
    bool has4 = !has5 && grid->GetNumberCols() >= 4;
    int keyCol = (has5 || has4) ? 1 : 0;
    int valCol = has5 ? 3 : (has4 ? 2 : 1);
    int needed = static_cast<int>(data.size()) + ((has5 || has4) ? 1 : 0);

    int current = grid->GetNumberRows();
    if (current < needed)
        grid->AppendRows(needed - current);
    else if (current > needed && needed > 0)
        grid->DeleteRows(0, current - needed);

    int row = 0;
    for (auto& [k, v] : data) {
        if (has5 || has4)
            grid->SetCellValue(row, 0, "1");
        if (has5)
            grid->SetCellValue(row, 2, "Text");
        grid->SetCellValue(row, keyCol, k);
        grid->SetCellValue(row, valCol, v);
        row++;
    }
    if (has5 || has4) { // trailing empty row
        grid->SetCellValue(row, 0, "1");
        if (has5)
            grid->SetCellValue(row, 2, "Text");
        grid->SetCellValue(row, keyCol, "");
        grid->SetCellValue(row, valCol, "");
    }
}

std::map<std::string, std::string> RequestTab::GridToMap(wxGrid* grid) {
    std::map<std::string, std::string> m;
    bool has5 = grid->GetNumberCols() >= 5; // form-data
    bool has4 = !has5 && grid->GetNumberCols() >= 4;
    int keyCol = (has5 || has4) ? 1 : 0;
    int valCol = has5 ? 3 : (has4 ? 2 : 1);
    for (int r = 0; r < grid->GetNumberRows(); r++) {
        if ((has5 || has4) && grid->GetCellValue(r, 0) != "1")
            continue; // disabled row
        if (has5 && grid->GetCellValue(r, 2) == "File")
            continue; // skip file rows in bulk edit
        auto key = grid->GetCellValue(r, keyCol).ToStdString();
        auto val = grid->GetCellValue(r, valCol).ToStdString();
        if (!key.empty())
            m[key] = val;
    }
    return m;
}

std::map<std::string, std::string> RequestTab::ParseRawKV(const wxString& text) {
    std::map<std::string, std::string> m;
    for (auto& line : wxSplit(text, '\n')) {
        wxString t = line.Trim(true).Trim(false);
        if (t.empty() || t.StartsWith("//"))
            continue;
        // prefer ':' separator (HTTP style), fall back to '='
        auto pos = t.find(':');
        if (pos == wxString::npos)
            pos = t.find('=');
        if (pos == wxString::npos)
            continue;
        m[t.Left(pos).Trim(true).ToStdString()] = t.Mid(pos + 1).Trim(false).ToStdString();
    }
    return m;
}

wxString RequestTab::MapToRaw(const std::map<std::string, std::string>& m) {
    wxString out;
    for (auto& [k, v] : m)
        out += wxString::FromUTF8(k) + ": " + wxString::FromUTF8(v) + "\n";
    return out;
}

wxPanel* RequestTab::MakeKVPanel(wxWindow* parent, const wxString& title, wxGrid*& gridOut,
                                 wxTextCtrl*& rawOut, wxSimplebook*& bookOut) {
    auto* panel = new wxPanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // header: title left, toggle button right
    auto* bar = new wxPanel(panel);
    auto* barSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* titleLabel = new wxStaticText(bar, wxID_ANY, title);
    titleLabel->SetFont(titleLabel->GetFont().Bold());
    auto* toggleBtn = new wxButton(bar, wxID_ANY, "Bulk Edit");
    barSizer->Add(titleLabel, 0, wxALIGN_CENTER_VERTICAL);
    barSizer->AddStretchSpacer();
    barSizer->Add(toggleBtn, 0, wxALIGN_CENTER_VERTICAL);
    bar->SetSizer(barSizer);

    // paged content
    auto* book = new wxSimplebook(panel);
    gridOut = MakeKeyValueGrid(book, true);
    rawOut = new wxTextCtrl(book, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                            wxTE_MULTILINE | wxTE_DONTWRAP | wxBORDER_NONE);
    rawOut->SetFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE)));
    book->AddPage(gridOut, "");
    book->AddPage(rawOut, "");
    bookOut = book;

    sizer->Add(bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    sizer->Add(book, 1, wxEXPAND | wxTOP, 4);
    panel->SetSizer(sizer);

    // single toggle button switches between KV and Bulk Edit modes
    toggleBtn->Bind(wxEVT_BUTTON, [this, toggleBtn, gridOut, rawOut, book](wxCommandEvent&) {
        if (book->GetSelection() == 0) {
            rawOut->SetValue(MapToRaw(GridToMap(gridOut)));
            book->SetSelection(1);
            toggleBtn->SetLabel("Key-Value Edit");
        } else {
            SetGridRows(gridOut, ParseRawKV(rawOut->GetValue()));
            book->SetSelection(0);
            toggleBtn->SetLabel("Bulk Edit");
        }
        toggleBtn->InvalidateBestSize();
        toggleBtn->GetParent()->Layout();
    });
    rawOut->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { MarkDirty(); });

    return panel;
}

wxGrid* RequestTab::MakeFormDataGrid(wxWindow* parent) {
    auto* grid = new wxGrid(parent, wxID_ANY);
    grid->SetRowLabelSize(0);
    grid->SetDefaultRowSize(26);
    grid->DisableDragColSize();

    // 5 columns: [enabled | Key | Type | Value | Description]
    grid->CreateGrid(1, 5);
    grid->SetColLabelSize(24);
    grid->SetColLabelValue(0, "");
    grid->SetColLabelValue(1, "Key");
    grid->SetColLabelValue(2, "");
    grid->SetColLabelValue(3, "Value");
    grid->SetColLabelValue(4, "Description");
    grid->SetColSize(0, 28);
    grid->SetColSize(1, 150);
    grid->SetColSize(2, 70);
    grid->SetColSize(4, 120);

    auto* boolAttr = new wxGridCellAttr();
    boolAttr->SetRenderer(new wxGridCellBoolRenderer());
    boolAttr->SetEditor(new wxGridCellBoolEditor());
    boolAttr->SetAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
    grid->SetColAttr(0, boolAttr);

    auto* keyAttr = new wxGridCellAttr();
    keyAttr->SetRenderer(new PlaceholderRenderer("Key"));
    grid->SetColAttr(1, keyAttr);

    wxArrayString types;
    types.Add("Text");
    types.Add("File");
    auto* typeAttr = new wxGridCellAttr();
    typeAttr->SetEditor(new wxGridCellChoiceEditor(types));
    grid->SetColAttr(2, typeAttr);

    auto* valAttr = new wxGridCellAttr();
    valAttr->SetRenderer(new PlaceholderRenderer("Value"));
    grid->SetColAttr(3, valAttr);

    auto* descAttr = new wxGridCellAttr();
    descAttr->SetRenderer(new PlaceholderRenderer("Description"));
    grid->SetColAttr(4, descAttr);

    grid->SetCellValue(0, 0, "1");
    grid->SetCellValue(0, 2, "Text");

    grid->SetSelectionMode(wxGrid::wxGridSelectCells);
    grid->UseNativeColHeader(false);

    BindColLabelPaint(grid);

    // open file dialog when clicking value cell of a File-type row
    grid->Bind(wxEVT_GRID_CELL_LEFT_CLICK, [this, grid](wxGridEvent& evt) {
        if (evt.GetCol() == 3 && grid->GetCellValue(evt.GetRow(), 2) == "File") {
            grid->DisableCellEditControl();
            wxFileDialog dlg(this, "Select file", "", "", "*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dlg.ShowModal() == wxID_OK) {
                grid->SetCellValue(evt.GetRow(), 3, dlg.GetPath());
                MarkDirty();
            }
            return; // don't start normal editor
        }
        evt.Skip();
    });

    grid->Bind(wxEVT_GRID_TABBING, [this, grid](wxGridEvent& evt) {
        int lastCol = grid->GetNumberCols() - 1;
        int row = evt.GetRow(), col = evt.GetCol();
        if (!evt.ShiftDown() && col == lastCol) {
            evt.Veto();
            grid->DisableCellEditControl();
            int nextRow = row + 1;
            if (nextRow >= grid->GetNumberRows()) {
                grid->AppendRows(1);
                grid->SetCellValue(nextRow, 0, "1");
                grid->SetCellValue(nextRow, 2, "Text");
            }
            grid->SetGridCursor(nextRow, 1);
            grid->MakeCellVisible(nextRow, 1);
            grid->EnableCellEditControl();
        } else if (evt.ShiftDown() && col <= 1 && row > 0) {
            evt.Veto();
            grid->DisableCellEditControl();
            grid->SetGridCursor(row - 1, lastCol);
            grid->MakeCellVisible(row - 1, lastCol);
            grid->EnableCellEditControl();
        } else {
            evt.Skip();
        }
    });

    grid->Bind(wxEVT_SIZE, [grid](wxSizeEvent& evt) {
        int w = evt.GetSize().GetWidth() - 28 - 150 - 70 - 120 - 4;
        if (w > 40)
            grid->SetColSize(3, w);
        evt.Skip();
    });

    grid->Bind(wxEVT_GRID_CELL_CHANGED, [this, grid](wxGridEvent& evt) {
        int last = grid->GetNumberRows() - 1;
        int col = evt.GetCol();
        if (evt.GetRow() == last && (col == 1 || col == 3)) {
            if (!grid->GetCellValue(last, 1).IsEmpty() || !grid->GetCellValue(last, 3).IsEmpty()) {
                grid->AppendRows(1);
                grid->SetCellValue(last + 1, 0, "1");
                grid->SetCellValue(last + 1, 2, "Text");
            }
        }
        MarkDirty();
        evt.Skip();
    });

    return grid;
}

void RequestTab::SelectBodyMode(int mode) {
    m_bodyMode = mode;
    wxColour accent(97, 175, 239);
    wxColour normal = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    for (int i = 0; i < 5; i++) {
        m_bodyTypeLabels[i]->SetForegroundColour(i == mode ? accent : normal);
        m_bodyTypeLabels[i]->Refresh();
    }
    m_bodyBook->SetSelection(mode);
    m_rawExtraPanel->Show(mode == 3);
    // layout bodyPanel (grandparent of radioBar) so radioBar gets correct height allocation
    m_rawExtraPanel->GetParent()->GetParent()->Layout();
    if (!m_loading)
        SetDirty(true);
}

void RequestTab::SyncUrlFromParams() {
    std::string base = StripQueryString(m_urlInput->GetValue().ToStdString());
    auto params = m_paramsBook->GetSelection() == 0 ? GridToMap(m_paramsGrid)
                                                    : ParseRawKV(m_paramsRaw->GetValue());
    // ChangeValue doesn't fire wxEVT_TEXT so no curl detection or re-dirty
    m_urlInput->ChangeValue(base + BuildQueryString(params));
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

    reqNotebook->AddPage(
        MakeKVPanel(reqNotebook, "Query Params", m_paramsGrid, m_paramsRaw, m_paramsBook),
        "Params");

    // keep URL input in sync with params edits
    m_paramsGrid->Bind(wxEVT_GRID_CELL_CHANGED, [this](wxGridEvent& evt) {
        SyncUrlFromParams();
        evt.Skip();
    });
    m_paramsRaw->Bind(wxEVT_TEXT, [this](wxCommandEvent& evt) {
        SyncUrlFromParams();
        evt.Skip();
    });

    reqNotebook->AddPage(
        MakeKVPanel(reqNotebook, "Headers", m_headersGrid, m_headersRaw, m_headersBook), "Headers");

    auto* bodyPanel = new wxPanel(reqNotebook);
    auto* bodySizer = new wxBoxSizer(wxVERTICAL);

    // body type selector bar (plain clickable labels — no focus rectangle)
    auto* radioBar = new wxPanel(bodyPanel);
    auto* radioSizer = new wxBoxSizer(wxHORIZONTAL);
    const char* bodyLabels[] = {"none", "form-data", "x-www-form-urlencoded", "raw", "binary"};
    for (int i = 0; i < 5; i++) {
        m_bodyTypeLabels[i] = new wxStaticText(radioBar, wxID_ANY, bodyLabels[i]);
        m_bodyTypeLabels[i]->SetCursor(wxCursor(wxCURSOR_HAND));
        radioSizer->Add(m_bodyTypeLabels[i], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);
        m_bodyTypeLabels[i]->Bind(wxEVT_LEFT_DOWN, [this, i](wxMouseEvent&) { SelectBodyMode(i); });
    }
    // initial selection: none
    m_bodyTypeLabels[0]->SetForegroundColour(wxColour(97, 175, 239));
    radioSizer->AddStretchSpacer();

    // raw extras: type dropdown + beautify (hidden unless "raw" selected)
    m_rawExtraPanel = new wxPanel(radioBar);
    auto* rawExtraSizer = new wxBoxSizer(wxHORIZONTAL);
    wxArrayString rawTypes;
    for (auto& t : {"JSON", "Text", "HTML", "XML", "JavaScript"})
        rawTypes.Add(t);
    m_rawTypeChoice =
        new wxChoice(m_rawExtraPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, rawTypes);
    m_rawTypeChoice->SetSelection(0);
    m_rawTypeChoice->SetMinSize(wxSize(-1, 34));
    m_rawTypeChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        ApplyBodyLexer(m_rawTypeChoice->GetSelection());
        MarkDirty();
    });
    auto* beautifyBtn = new wxButton(m_rawExtraPanel, wxID_ANY, "Beautify");
    beautifyBtn->SetMinSize(wxSize(-1, 34));
    beautifyBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        // simple JSON pretty-printer
        std::string s = m_bodyInput->GetText().ToStdString();
        std::string out;
        out.reserve(s.size() * 2); // formatted output is typically larger than input
        int indent = 0;
        bool inStr = false;
        for (size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (inStr) {
                out += c;
                if (c == '\\' && i + 1 < s.size())
                    out += s[++i];
                else if (c == '"')
                    inStr = false;
            } else if (c == '"') {
                inStr = true;
                out += c;
            } else if (c == '{' || c == '[') {
                out += c;
                out += '\n';
                indent += 2;
                out += std::string(indent, ' ');
            } else if (c == '}' || c == ']') {
                out += '\n';
                indent = std::max(0, indent - 2);
                out += std::string(indent, ' ');
                out += c;
            } else if (c == ',') {
                out += c;
                out += '\n';
                out += std::string(indent, ' ');
            } else if (c == ':') {
                out += ": ";
            } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                out += c;
            }
        }
        if (!out.empty())
            m_bodyInput->SetText(wxString::FromUTF8(out));
    });
    rawExtraSizer->Add(m_rawTypeChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    rawExtraSizer->Add(beautifyBtn, 0, wxALIGN_CENTER_VERTICAL);
    m_rawExtraPanel->SetSizer(rawExtraSizer);
    m_rawExtraPanel->Hide();
    radioSizer->Add(m_rawExtraPanel, 0, wxALIGN_CENTER_VERTICAL);
    radioBar->SetSizer(radioSizer);
    radioBar->SetMinSize(wxSize(-1, 38));

    // body book — one page per mode
    m_bodyBook = new wxSimplebook(bodyPanel);

    // page 0: none
    {
        auto* p = new wxPanel(m_bodyBook);
        auto* s = new wxBoxSizer(wxVERTICAL);
        s->AddStretchSpacer();
        s->Add(new wxStaticText(p, wxID_ANY, "This request does not have a body"), 0,
               wxALIGN_CENTER_HORIZONTAL);
        s->AddStretchSpacer();
        p->SetSizer(s);
        m_bodyBook->AddPage(p, "");
    }
    // page 1: form-data (5-column grid with Text/File type per row)
    {
        auto* panel = new wxPanel(m_bodyBook);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        auto* bar2 = new wxPanel(panel);
        auto* bar2Sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* titleLabel = new wxStaticText(bar2, wxID_ANY, "Form Data");
        titleLabel->SetFont(titleLabel->GetFont().Bold());
        auto* toggleBtn = new wxButton(bar2, wxID_ANY, "Bulk Edit");
        bar2Sizer->Add(titleLabel, 0, wxALIGN_CENTER_VERTICAL);
        bar2Sizer->AddStretchSpacer();
        bar2Sizer->Add(toggleBtn, 0, wxALIGN_CENTER_VERTICAL);
        bar2->SetSizer(bar2Sizer);

        m_formDataGrid = MakeFormDataGrid(panel);
        m_formDataRaw = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                       wxTE_MULTILINE | wxTE_DONTWRAP | wxBORDER_NONE);
        m_formDataRaw->SetFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE)));

        m_formDataBook = new wxSimplebook(panel);
        m_formDataBook->AddPage(m_formDataGrid, "");
        m_formDataBook->AddPage(m_formDataRaw, "");

        sizer->Add(bar2, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
        sizer->Add(m_formDataBook, 1, wxEXPAND | wxTOP, 4);
        panel->SetSizer(sizer);

        toggleBtn->Bind(wxEVT_BUTTON, [this, toggleBtn](wxCommandEvent&) {
            if (m_formDataBook->GetSelection() == 0) {
                // text-only rows to bulk edit
                m_formDataRaw->SetValue(MapToRaw(GridToMap(m_formDataGrid)));
                m_formDataBook->SetSelection(1);
                toggleBtn->SetLabel("Key-Value Edit");
            } else {
                SetGridRows(m_formDataGrid, ParseRawKV(m_formDataRaw->GetValue()));
                m_formDataBook->SetSelection(0);
                toggleBtn->SetLabel("Bulk Edit");
            }
            toggleBtn->InvalidateBestSize();
            toggleBtn->GetParent()->Layout();
        });
        m_formDataRaw->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { MarkDirty(); });

        m_bodyBook->AddPage(panel, "");
    }
    // page 2: x-www-form-urlencoded
    m_bodyBook->AddPage(
        MakeKVPanel(m_bodyBook, "URL Encoded", m_urlEncodedGrid, m_urlEncodedRaw, m_urlEncodedBook),
        "");
    // page 3: raw
    m_bodyInput =
        new wxStyledTextCtrl(m_bodyBook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    SetupStc(m_bodyInput, false);
    ApplyStcLexer(m_bodyInput, wxSTC_LEX_JSON); // default: JSON
    m_bodyBook->AddPage(m_bodyInput, "");
    // page 4: binary
    {
        auto* p = new wxPanel(m_bodyBook);
        auto* s = new wxBoxSizer(wxVERTICAL);
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        m_binaryPath = new wxTextCtrl(p, wxID_ANY, "", wxDefaultPosition, wxSize(280, -1));
        m_binaryPath->SetHint("Select file");
        auto* browseBtn = new wxButton(p, wxID_ANY, "Browse...");
        browseBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            wxFileDialog dlg(this, "Select file", "", "", "*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dlg.ShowModal() == wxID_OK) {
                m_binaryPath->SetValue(dlg.GetPath());
                MarkDirty();
            }
        });
        row->Add(m_binaryPath, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        row->Add(browseBtn, 0, wxALIGN_CENTER_VERTICAL);
        s->Add(row, 0, wxALL, 8);
        s->AddStretchSpacer();
        p->SetSizer(s);
        m_bodyBook->AddPage(p, "");
    }

    bodySizer->Add(radioBar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    bodySizer->Add(m_bodyBook, 1, wxEXPAND | wxTOP, 2);
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

    m_responseBody = new wxStyledTextCtrl(resNotebook, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                          wxBORDER_NONE);
    SetupStc(m_responseBody, true);
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
    m_bodyInput->Bind(wxEVT_STC_CHANGE, [this](wxStyledTextEvent&) { MarkDirty(); });

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

    auto params = m_paramsBook->GetSelection() == 0 ? GridToMap(m_paramsGrid)
                                                    : ParseRawKV(m_paramsRaw->GetValue());
    std::string qs = BuildQueryString(params);
    if (!qs.empty())
        req.url = StripQueryString(req.url) + qs;

    req.headers = m_headersBook->GetSelection() == 0 ? GridToMap(m_headersGrid)
                                                     : ParseRawKV(m_headersRaw->GetValue());

    switch (m_bodyMode) {
    case 1: { // form-data
        std::string boundary = "----DearAPIBoundary";
        std::string body;
        if (m_formDataBook->GetSelection() == 1) {
            // bulk edit: all text
            for (auto& [k, v] : ParseRawKV(m_formDataRaw->GetValue())) {
                body += "--" + boundary + "\r\n";
                body += "Content-Disposition: form-data; name=\"" + k + "\"\r\n\r\n";
                body += v + "\r\n";
            }
        } else {
            for (int r = 0; r < m_formDataGrid->GetNumberRows(); r++) {
                if (m_formDataGrid->GetCellValue(r, 0) != "1")
                    continue;
                auto k = m_formDataGrid->GetCellValue(r, 1).ToStdString();
                if (k.empty())
                    continue;
                auto type = m_formDataGrid->GetCellValue(r, 2).ToStdString();
                auto v = m_formDataGrid->GetCellValue(r, 3).ToStdString();
                body += "--" + boundary + "\r\n";
                if (type == "File") {
                    std::string fname = v.substr(v.find_last_of("/\\") + 1);
                    body += "Content-Disposition: form-data; name=\"" + k + "\"; filename=\"" +
                            fname + "\"\r\n";
                    body += "Content-Type: application/octet-stream\r\n\r\n";
                    std::ifstream f(v, std::ios::binary);
                    body += std::string(std::istreambuf_iterator<char>(f), {});
                    body += "\r\n";
                } else {
                    body += "Content-Disposition: form-data; name=\"" + k + "\"\r\n\r\n";
                    body += v + "\r\n";
                }
            }
        }
        if (!body.empty()) {
            body += "--" + boundary + "--\r\n";
            req.body = body;
            if (req.headers.find("Content-Type") == req.headers.end())
                req.headers["Content-Type"] = "multipart/form-data; boundary=" + boundary;
        }
        break;
    }
    case 2: { // x-www-form-urlencoded
        auto fields = m_urlEncodedBook->GetSelection() == 0
                          ? GridToMap(m_urlEncodedGrid)
                          : ParseRawKV(m_urlEncodedRaw->GetValue());
        std::string body;
        for (auto& [k, v] : fields) {
            if (!body.empty())
                body += "&";
            body += k + "=" + v;
        }
        req.body = body;
        if (req.headers.find("Content-Type") == req.headers.end())
            req.headers["Content-Type"] = "application/x-www-form-urlencoded";
        break;
    }
    case 3: { // raw
        req.body = m_bodyInput->GetText().ToStdString();
        if (req.headers.find("Content-Type") == req.headers.end()) {
            int sel = m_rawTypeChoice->GetSelection();
            if (sel == 0)
                req.headers["Content-Type"] = "application/json";
            else if (sel == 2)
                req.headers["Content-Type"] = "text/html";
            else if (sel == 3)
                req.headers["Content-Type"] = "application/xml";
            else if (sel == 4)
                req.headers["Content-Type"] = "application/javascript";
            else
                req.headers["Content-Type"] = "text/plain";
        }
        break;
    }
    case 4: { // binary
        std::string path = m_binaryPath->GetValue().ToStdString();
        if (!path.empty()) {
            std::ifstream f(path, std::ios::binary);
            req.body = std::string(std::istreambuf_iterator<char>(f), {});
        }
        break;
    }
    default:
        break; // none
    }

    return req;
}

HttpRequest RequestTab::GetRequest() const {
    return BuildCurrentRequest();
}

void RequestTab::LoadRequest(const HttpRequest& req) {
    m_loading = true;

    SetGridRows(m_paramsGrid, {});
    m_paramsRaw->SetValue("");

    // SetMethod doesn't call MarkDirty while m_loading, but it still
    // recolours the button — use it directly
    m_currentMethod = req.method;
    m_methodBtn->SetLabel(req.method);
    m_methodBtn->SetBackgroundColour(MethodColor(req.method));
    m_methodBtn->SetForegroundColour(MethodTextColor(req.method));
    m_methodBtn->Refresh();

    m_urlInput->SetValue(req.url);

    SetGridRows(m_headersGrid, req.headers);
    m_headersRaw->SetValue(MapToRaw(req.headers));

    if (req.body.empty()) {
        m_bodyMode = 0;
    } else {
        auto ct = req.headers.find("Content-Type");
        std::string ctVal = ct != req.headers.end() ? ct->second : "";
        if (ctVal.find("multipart/form-data") != std::string::npos) {
            m_bodyMode = 1;
        } else if (ctVal.find("application/x-www-form-urlencoded") != std::string::npos) {
            m_bodyMode = 2;
            auto fields = ParseRawKV(wxString::FromUTF8(req.body));
            SetGridRows(m_urlEncodedGrid, fields);
            m_urlEncodedRaw->SetValue(MapToRaw(fields));
        } else {
            m_bodyMode = 3; // default to raw for any other content type
            m_bodyInput->SetText(wxString::FromUTF8(req.body.c_str(), req.body.size()));
            if (ctVal.find("application/json") != std::string::npos)
                m_rawTypeChoice->SetSelection(0);
            else if (ctVal.find("text/html") != std::string::npos)
                m_rawTypeChoice->SetSelection(2);
            else if (ctVal.find("application/xml") != std::string::npos ||
                     ctVal.find("text/xml") != std::string::npos)
                m_rawTypeChoice->SetSelection(3);
            else if (ctVal.find("javascript") != std::string::npos)
                m_rawTypeChoice->SetSelection(4);
            else
                m_rawTypeChoice->SetSelection(1); // Text
            ApplyBodyLexer(m_rawTypeChoice->GetSelection());
        }
    }
    // apply visual selection (m_loading suppresses dirty marking inside SelectBodyMode)
    SelectBodyMode(m_bodyMode);

    m_loading = false;
    SetDirty(false);
}

// ── event handlers
// ────────────────────────────────────────────────────────────

static std::string SubstituteVars(const std::string& s,
                                  const std::map<std::string, std::string>& vars) {
    std::string out = s;
    for (auto& [k, v] : vars) {
        std::string ph = "{{" + k + "}}";
        for (size_t pos = 0; (pos = out.find(ph, pos)) != std::string::npos;)
            out.replace(pos, ph.size(), v);
    }
    return out;
}

void RequestTab::OnSend(wxCommandEvent&) {
    m_sendButton->Disable();
    m_statusLabel->SetLabel("Sending...");

    // snapshot everything the thread needs — no this access inside the thread body
    HttpRequest req = BuildCurrentRequest();

    // substitute {{variable}} placeholders with collection variables
    if (getVariables) {
        auto vars = getVariables();
        if (!vars.empty()) {
            req.url = SubstituteVars(req.url, vars);
            for (auto& entry : req.headers)
                entry.second = SubstituteVars(entry.second, vars);
            req.body = SubstituteVars(req.body, vars);
        }
    }
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
        m_responseBody->SetReadOnly(false);
        m_responseBody->SetText(res.error);
        m_responseBody->SetReadOnly(true);
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

    auto ct = res.headers.find("Content-Type");
    std::string ctVal = ct != res.headers.end() ? ct->second : "";
    ApplyResponseLexer(ctVal);
    m_responseBody->SetReadOnly(false);
    m_responseBody->SetText(wxString::FromUTF8(res.body.c_str(), res.body.size()));
    m_responseBody->SetReadOnly(true);
    SetGridRows(m_responseHeadersGrid, res.headers);
}

void RequestTab::ApplyBodyLexer(int sel) {
    // sel: 0=JSON 1=Text 2=HTML 3=XML 4=JavaScript
    int lexer = wxSTC_LEX_NULL;
    if (sel == 0)
        lexer = wxSTC_LEX_JSON;
    else if (sel == 2)
        lexer = wxSTC_LEX_HTML;
    else if (sel == 3)
        lexer = wxSTC_LEX_XML;
    else if (sel == 4)
        lexer = wxSTC_LEX_CPP;
    ApplyStcLexer(m_bodyInput, lexer);
}

void RequestTab::ApplyResponseLexer(const std::string& ct) {
    int lexer = wxSTC_LEX_NULL;
    if (ct.find("json") != std::string::npos)
        lexer = wxSTC_LEX_JSON;
    else if (ct.find("html") != std::string::npos)
        lexer = wxSTC_LEX_HTML;
    else if (ct.find("xml") != std::string::npos)
        lexer = wxSTC_LEX_XML;
    else if (ct.find("javascript") != std::string::npos)
        lexer = wxSTC_LEX_CPP;
    ApplyStcLexer(m_responseBody, lexer);
}
