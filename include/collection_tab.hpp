#pragma once
#include "db_store.hpp"
#include <wx/grid.h>
#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/wx.h>

class CollectionTab : public wxPanel {
public:
    explicit CollectionTab(wxWindow* parent, int64_t collectionId, const std::string& name,
                           DBStore* db);
    int64_t GetCollectionId() const {
        return m_collectionId;
    }
    std::string GetTabName() const {
        return m_name;
    }
    void SetTabName(const std::string& n) {
        m_name = n;
    }
    void Reload();

private:
    int64_t m_collectionId;
    std::string m_name;
    DBStore* m_db;
    wxGrid* m_varsGrid{};
    wxTimer* m_saveTimer{};

    void BuildUI();
    void LoadVars();
    void SaveVars();
};
