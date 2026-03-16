#pragma once
#include <memory>
#include <vector>
#include <wx/aui/auibook.h>
#include <wx/filename.h>
#include <wx/listbox.h>
#include <wx/notebook.h>
#include <wx/stdpaths.h>
#include <wx/treectrl.h>
#include <wx/wx.h>

#include "db_store.hpp"
#include "request_tab.hpp"

// ── tree item data
// ────────────────────────────────────────────────────────────

class CollectionItemData : public wxTreeItemData {
public:
    enum class Type { Folder, Request };
    Type type;
    int64_t id;
    CollectionItemData(Type t, int64_t i) : type(t), id(i) {}
};

enum {
    ID_TREE_NEW_REQUEST = wxID_HIGHEST + 100,
    ID_TREE_NEW_FOLDER,
    ID_TREE_RENAME,
    ID_TREE_DELETE,
    ID_TREE_LOAD,
};

struct HistoryItem {
    HttpRequest request;
    std::string label;
};

// ── main frame
// ────────────────────────────────────────────────────────────────

class MainFrame : public wxFrame {
public:
    MainFrame();

private:
    // top bar
    wxButton* m_toggleBtn{};
    wxButton* m_newTabBtn{};

    // layout
    wxPanel* m_contentPanel{};
    wxPanel* m_sidebar{};
    bool m_sidebarVisible{true};

    // sidebar tabs
    wxNotebook* m_sidebarTabs{};

    // collections
    wxTreeCtrl* m_tree{};
    wxTreeItemId m_treeRoot{};
    wxTreeItemId m_dragItem{};
    std::unique_ptr<DBStore> m_db;

    // history
    wxListBox* m_historyList{};
    std::vector<HistoryItem> m_history;

    // main tab area
    wxAuiNotebook* m_notebook{};

    void BuildUI();

    // tab management
    RequestTab* NewTab(const std::string& name = "New Request");
    void OpenInTab(const HttpRequest& req, const std::string& name, int64_t savedId = 0);

    // collections tree
    void RebuildTree();
    void BuildTreeBranch(wxTreeItemId parent, int64_t parentId);
    int64_t ContextFolderId() const;
    int64_t SaveRequest(const HttpRequest& req, const std::string& suggestedName);
    void AddToHistory(const HttpRequest& req);

    // arrow bitmap for toggle
    static wxBitmap ArrowBitmap(bool pointLeft);

    // event handlers
    void OnToggleSidebar(wxCommandEvent&);
    void OnNewTab(wxCommandEvent&);

    void OnHistorySelect(wxCommandEvent&);
    void OnClearHistory(wxCommandEvent&);

    void OnTreeActivated(wxTreeEvent&);
    void OnTreeBeginDrag(wxTreeEvent&);
    void OnTreeEndDrag(wxTreeEvent&);
    void OnTreeEndLabelEdit(wxTreeEvent&);
    void OnTreeMenu(wxTreeEvent&);

    void OnMenuNewRequest(wxCommandEvent&);
    void OnMenuNewFolder(wxCommandEvent&);
    void OnMenuRename(wxCommandEvent&);
    void OnMenuDelete(wxCommandEvent&);
    void OnMenuLoad(wxCommandEvent&);
};
