#include "collection_tab.hpp"
#include <wx/stattext.h>

CollectionTab::CollectionTab(wxWindow* parent, int64_t collectionId, const std::string& name,
                             DBStore* db)
    : wxPanel(parent), m_collectionId(collectionId), m_name(name), m_db(db) {
    m_saveTimer = new wxTimer(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { SaveVars(); }, m_saveTimer->GetId());
    BuildUI();
    LoadVars();
}

void CollectionTab::BuildUI() {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // bold header label
    auto* label = new wxStaticText(this, wxID_ANY, "Variables");
    auto font = label->GetFont();
    font.MakeBold();
    label->SetFont(font);
    sizer->Add(label, 0, wxALL, 8);

    // 3-column grid: Variable | Value | Description
    m_varsGrid = new wxGrid(this, wxID_ANY);
    m_varsGrid->CreateGrid(1, 3);
    m_varsGrid->SetColLabelValue(0, "Variable");
    m_varsGrid->SetColLabelValue(1, "Value");
    m_varsGrid->SetColLabelValue(2, "Description");
    m_varsGrid->HideRowLabels();
    m_varsGrid->SetDefaultCellOverflow(false);
    sizer->Add(m_varsGrid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    SetSizer(sizer);

    // auto-resize columns equally on size
    m_varsGrid->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        int w = m_varsGrid->GetClientSize().GetWidth();
        if (w > 0) {
            int col = w / 3;
            m_varsGrid->SetColSize(0, col);
            m_varsGrid->SetColSize(1, col);
            m_varsGrid->SetColSize(2, w - col * 2);
        }
    });

    // auto-append trailing row; debounce save to avoid a DELETE+N INSERTs per keystroke
    m_varsGrid->Bind(wxEVT_GRID_CELL_CHANGED, [this](wxGridEvent& e) {
        e.Skip();
        if (e.GetRow() == m_varsGrid->GetNumberRows() - 1)
            m_varsGrid->AppendRows(1);
        m_saveTimer->StartOnce(300);
    });
}

void CollectionTab::LoadVars() {
    // clear existing rows
    if (m_varsGrid->GetNumberRows() > 0)
        m_varsGrid->DeleteRows(0, m_varsGrid->GetNumberRows());

    auto vars = m_db->getCollectionVariables(m_collectionId);
    m_varsGrid->AppendRows(static_cast<int>(vars.size()) + 1); // +1 trailing empty row
    for (int i = 0; i < static_cast<int>(vars.size()); ++i) {
        m_varsGrid->SetCellValue(i, 0, vars[i].key);
        m_varsGrid->SetCellValue(i, 1, vars[i].value);
        m_varsGrid->SetCellValue(i, 2, vars[i].description);
    }
}

void CollectionTab::SaveVars() {
    std::vector<CollectionVariable> vars;
    for (int i = 0; i < m_varsGrid->GetNumberRows(); ++i) {
        std::string key = m_varsGrid->GetCellValue(i, 0).ToStdString();
        if (key.empty())
            continue;
        CollectionVariable v;
        v.key = key;
        v.value = m_varsGrid->GetCellValue(i, 1).ToStdString();
        v.description = m_varsGrid->GetCellValue(i, 2).ToStdString();
        vars.push_back(std::move(v));
    }
    m_db->setCollectionVariables(m_collectionId, vars);
}

void CollectionTab::Reload() {
    LoadVars();
}
