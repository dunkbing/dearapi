#pragma once
#include <memory>
#include <vector>
#include <wx/aui/auibook.h>
#include <wx/filename.h>
#include <wx/listbox.h>
#include <wx/notebook.h>
#include <wx/simplebook.h>
#include <wx/splitter.h>
#include <wx/stdpaths.h>
#include <wx/treectrl.h>
#include <wx/wx.h>

#include "app_gate.hpp"
#include "collection_tab.hpp"
#include "db_store.hpp"
#include "request_tab.hpp"

// ── tree item data
// ────────────────────────────────────────────────────────────

class CollectionItemData : public wxTreeItemData {
public:
    enum class Type { Collection, Folder, Request };
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
    ID_TREE_NEW_COLLECTION,
    ID_TREE_OPEN_COLLECTION,
};

struct HistoryItem {
    HttpRequest request;
    std::string label;
};

// ── main frame
// ────────────────────────────────────────────────────────────────

class MainFrame : public wxFrame {
public:
    explicit MainFrame(std::shared_ptr<AppGate> gate);

    void ToggleSidebar(); // called from GTK header bar button
    void DoNewTab();      // called from GTK header bar button
    void DoImport();      // called from GTK header bar button

private:
    // layout
    wxSplitterWindow* m_splitter{};
    wxPanel* m_sidebar{};
    bool m_sidebarVisible{true};
    int m_sidebarWidth{240};

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

    // app-level posting gate (outlives all tabs)
    std::shared_ptr<AppGate> m_gate;

    // right-side: switches between empty placeholder and notebook
    wxSimplebook* m_rightBook{};
    wxAuiNotebook* m_notebook{};

    void BuildUI();
    void SetupTitlebar();
    void UpdateRightView();

    // tab management
    RequestTab* NewTab(const std::string& name = "New Request");
    void OpenInTab(const HttpRequest& req, const std::string& name, int64_t savedId = 0);
    void OpenInCollectionTab(int64_t collectionId, const std::string& name);

    // collections tree
    void RebuildTree();
    void BuildTreeBranch(wxTreeItemId parent, int64_t collectionId, int64_t parentFolderId);
    int64_t ContextCollectionId() const;
    int64_t ContextFolderId() const;
    int64_t GetOrAutoSelectCollection();
    int64_t SaveRequest(const HttpRequest& req, const std::string& suggestedName);
    void AddToHistory(const HttpRequest& req);

    // event handlers
    void OnNewTab(wxCommandEvent&);

    void OnHistorySelect(wxCommandEvent&);
    void OnClearHistory(wxCommandEvent&);

    void OnTreeActivated(wxTreeEvent&);
    void OnTreeBeginDrag(wxTreeEvent&);
    void OnTreeEndDrag(wxTreeEvent&);
    void OnTreeEndLabelEdit(wxTreeEvent&);
    void OnTreeContextMenu(wxContextMenuEvent&);

    void OnMenuNewRequest(wxCommandEvent&);
    void OnMenuNewFolder(wxCommandEvent&);
    void OnMenuNewCollection(wxCommandEvent&);
    void OnMenuRename(wxCommandEvent&);
    void OnMenuDelete(wxCommandEvent&);
    void OnMenuLoad(wxCommandEvent&);
    void OnMenuOpenCollection(wxCommandEvent&);
};
