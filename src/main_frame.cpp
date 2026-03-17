#include "main_frame.hpp"
#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <wx/artprov.h>
#include <wx/aui/tabart.h>
#include <wx/filedlg.h>
#include <wx/filename.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

// ── custom tab art: bold title when dirty
// ─────────────────────────────────────

class DearTabArt : public wxAuiDefaultTabArt {
public:
    DearTabArt() {
        m_base = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        m_bold = m_base;
        m_bold.MakeBold();
        SetNormalFont(m_base);
        SetSelectedFont(m_base);
        SetMeasuringFont(m_bold); // measure with bold so width stays stable
    }

    wxAuiTabArt* Clone() override {
        return new DearTabArt();
    }

    void DrawTab(wxDC& dc, wxWindow* wnd, const wxAuiNotebookPage& pane, const wxRect& inRect,
                 int closeButtonState, wxRect* outTabRect, wxRect* outButtonRect,
                 int* xExtent) override {
        if (pane.active) {
            auto* tab = dynamic_cast<RequestTab*>(pane.window);
            SetSelectedFont(tab && tab->IsDirty() ? m_bold : m_base);
        }
        wxAuiDefaultTabArt::DrawTab(dc, wnd, pane, inRect, closeButtonState, outTabRect,
                                    outButtonRect, xExtent);
    }

private:
    wxFont m_base, m_bold;
};

// ── bitmaps
// ────────────────────────────────────────────────────────────────────

// ── constructor
// ───────────────────────────────────────────────────────────────

MainFrame::MainFrame(std::shared_ptr<AppGate> gate)
    : wxFrame(nullptr, wxID_ANY, "DearAPI", wxDefaultPosition, wxSize(1280, 800)),
      m_gate(std::move(gate)) {
    m_db = std::make_unique<DBStore>();
    wxString dataDir = wxStandardPaths::Get().GetUserDataDir();
    wxFileName::Mkdir(dataDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    m_db->open((dataDir + wxFILE_SEP_PATH + "collections.db").ToStdString());

    SetupTitlebar();
    BuildUI();
    Centre();
}

// ── UI construction
// ───────────────────────────────────────────────────────────

void MainFrame::BuildUI() {
    auto* panel = new wxPanel(this);
    auto* root = new wxBoxSizer(wxVERTICAL);

    // ── splitter: sidebar | right ─────────────────────────────────────────────
    m_splitter = new wxSplitterWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxSP_LIVE_UPDATE | wxSP_THIN_SASH);
    m_splitter->SetMinimumPaneSize(150);

    // ── sidebar ──────────────────────────────────────────────────────────────
    m_sidebar = new wxPanel(m_splitter);
    auto* sidebarSizer = new wxBoxSizer(wxVERTICAL);
    m_sidebarTabs = new wxNotebook(m_sidebar, wxID_ANY);

    // ── Collections tab ───────────────────────────────────────────────────
    auto* collPanel = new wxPanel(m_sidebarTabs);
    auto* collSizer = new wxBoxSizer(wxVERTICAL);

    // compact toolbar above tree
    auto* toolbar = new wxPanel(collPanel);
    toolbar->SetMinSize(wxSize(-1, 28));
    auto* toolbarSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* newCollBtn = new wxButton(toolbar, ID_TREE_NEW_COLLECTION, "+ Collection",
                                    wxDefaultPosition, wxSize(-1, 24));
    toolbarSizer->Add(newCollBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    toolbarSizer->AddStretchSpacer();
    toolbar->SetSizer(toolbarSizer);
    collSizer->Add(toolbar, 0, wxEXPAND);

    m_tree = new wxTreeCtrl(collPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_EDIT_LABELS | wxTR_SINGLE |
                                wxBORDER_NONE);
    m_tree->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));

    // folder / file icons from system art provider
    auto* imgList = new wxImageList(16, 16);
    imgList->Add(wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, wxSize(16, 16)));
    imgList->Add(wxArtProvider::GetBitmap(wxART_FOLDER_OPEN, wxART_OTHER, wxSize(16, 16)));
    imgList->Add(wxArtProvider::GetBitmap(wxART_NORMAL_FILE, wxART_OTHER, wxSize(16, 16)));
    m_tree->AssignImageList(imgList); // tree takes ownership

    collSizer->Add(m_tree, 1, wxEXPAND);
    collPanel->SetSizer(collSizer);
    m_sidebarTabs->AddPage(collPanel, "Collections");

    m_tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &MainFrame::OnTreeActivated, this);
    m_tree->Bind(wxEVT_TREE_BEGIN_DRAG, &MainFrame::OnTreeBeginDrag, this);
    m_tree->Bind(wxEVT_TREE_END_DRAG, &MainFrame::OnTreeEndDrag, this);
    m_tree->Bind(wxEVT_TREE_END_LABEL_EDIT, &MainFrame::OnTreeEndLabelEdit, this);
    m_tree->Bind(wxEVT_CONTEXT_MENU, &MainFrame::OnTreeContextMenu, this);

    newCollBtn->Bind(wxEVT_BUTTON, &MainFrame::OnMenuNewCollection, this);

    RebuildTree();

    // ── History tab ───────────────────────────────────────────────────────
    auto* histPanel = new wxPanel(m_sidebarTabs);
    auto* histSizer = new wxBoxSizer(wxVERTICAL);

    auto* histHeader = new wxBoxSizer(wxHORIZONTAL);
    histHeader->AddStretchSpacer();
    auto* clearBtn = new wxButton(histPanel, wxID_ANY, "Clear", wxDefaultPosition, wxSize(50, 22));
    histHeader->Add(clearBtn);

    m_historyList = new wxListBox(histPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                  wxLB_SINGLE | wxLB_HSCROLL | wxBORDER_NONE);
    m_historyList->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));

    histSizer->Add(histHeader, 0, wxEXPAND | wxBOTTOM, 4);
    histSizer->Add(m_historyList, 1, wxEXPAND);
    histPanel->SetSizer(histSizer);
    m_sidebarTabs->AddPage(histPanel, "History");

    clearBtn->Bind(wxEVT_BUTTON, &MainFrame::OnClearHistory, this);
    m_historyList->Bind(wxEVT_LISTBOX, &MainFrame::OnHistorySelect, this);

    sidebarSizer->Add(m_sidebarTabs, 1, wxEXPAND);
    m_sidebar->SetSizer(sidebarSizer);

    // ── right side: empty placeholder or tab notebook ─────────────────────────
    m_rightBook = new wxSimplebook(m_splitter);

    // page 0: empty state
    auto* emptyPanel = new wxPanel(m_rightBook);
    auto* emptySizer = new wxBoxSizer(wxVERTICAL);
    auto* emptyBtn =
        new wxButton(emptyPanel, wxID_ANY, "New Request", wxDefaultPosition, wxSize(160, 50));
    emptyBtn->SetFont(emptyBtn->GetFont().Larger());
    emptySizer->AddStretchSpacer();
    emptySizer->Add(emptyBtn, 0, wxALIGN_CENTER_HORIZONTAL);
    emptySizer->AddStretchSpacer();
    emptyPanel->SetSizer(emptySizer);
    m_rightBook->AddPage(emptyPanel, "");
    emptyBtn->Bind(wxEVT_BUTTON, &MainFrame::OnNewTab, this);

    // page 1: notebook
    m_notebook = new wxAuiNotebook(m_rightBook, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxAUI_NB_TOP | wxAUI_NB_CLOSE_ON_ALL_TABS |
                                       wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_TAB_MOVE);
    m_notebook->SetArtProvider(new DearTabArt());
    m_rightBook->AddPage(m_notebook, "");
    m_rightBook->SetSelection(0); // start on empty state

    m_notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSED,
                     [this](wxAuiNotebookEvent&) { UpdateRightView(); });

    m_splitter->SplitVertically(m_sidebar, m_rightBook, m_sidebarWidth);

    root->Add(m_splitter, 1, wxEXPAND | wxALL, 8);
    panel->SetSizer(root);
}

// ── tab management
// ────────────────────────────────────────────────────────────

void MainFrame::SetupTitlebar() {
#ifdef __WXGTK__
    GtkWidget* header = gtk_header_bar_new();
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
#else
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "DearAPI");
#endif

    // sidebar toggle
#if GTK_CHECK_VERSION(4, 0, 0)
    GtkWidget* toggleBtn = gtk_button_new_from_icon_name("open-menu-symbolic");
#else
    GtkWidget* toggleBtn =
        gtk_button_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON);
#endif
    gtk_widget_set_tooltip_text(toggleBtn, "Toggle sidebar");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), toggleBtn);
    g_signal_connect(toggleBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
                         static_cast<MainFrame*>(d)->ToggleSidebar();
                     }),
                     this);

    // new tab
#if GTK_CHECK_VERSION(4, 0, 0)
    GtkWidget* addBtn = gtk_button_new_from_icon_name("list-add-symbolic");
#else
    GtkWidget* addBtn = gtk_button_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_BUTTON);
#endif
    gtk_widget_set_tooltip_text(addBtn, "New tab");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), addBtn);
    g_signal_connect(
        addBtn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer d) { static_cast<MainFrame*>(d)->DoNewTab(); }), this);

    // import
#if GTK_CHECK_VERSION(4, 0, 0)
    GtkWidget* importBtn = gtk_button_new_from_icon_name("document-open-symbolic");
#else
    GtkWidget* importBtn =
        gtk_button_new_from_icon_name("document-open-symbolic", GTK_ICON_SIZE_BUTTON);
#endif
    gtk_widget_set_tooltip_text(importBtn, "Import collection");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), importBtn);
    g_signal_connect(
        importBtn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer d) { static_cast<MainFrame*>(d)->DoImport(); }), this);

    gtk_window_set_titlebar(GTK_WINDOW(static_cast<GtkWidget*>(GetHandle())), header);
#if !GTK_CHECK_VERSION(4, 0, 0)
    gtk_widget_show_all(header);
#endif
#endif
}

void MainFrame::ToggleSidebar() {
    m_sidebarVisible = !m_sidebarVisible;
    if (m_sidebarVisible) {
        m_splitter->SplitVertically(m_sidebar, m_rightBook, m_sidebarWidth);
    } else {
        m_sidebarWidth = m_splitter->GetSashPosition();
        m_splitter->Unsplit(m_sidebar);
    }
}

void MainFrame::DoNewTab() {
    NewTab();
}

void MainFrame::DoImport() {
    wxFileDialog dlg(this, "Import Swagger / OpenAPI", "", "",
                     "JSON files (*.json)|*.json|All files (*.*)|*.*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK)
        return;
    ImportSwagger(dlg.GetPath().ToStdString());
}

void MainFrame::UpdateRightView() {
    m_rightBook->SetSelection(m_notebook->GetPageCount() > 0 ? 1 : 0);
}

RequestTab* MainFrame::NewTab(const std::string& name) {
    m_rightBook->SetSelection(1);
    auto* tab = new RequestTab(m_notebook, name, m_gate);

    tab->getVariables = [this, tab]() -> std::map<std::string, std::string> {
        int64_t savedId = tab->GetSavedId();
        if (savedId == 0)
            return {};
        auto saved = m_db->getRequest(savedId);
        std::map<std::string, std::string> result;
        for (auto& v : m_db->getCollectionVariables(saved.collectionId))
            result[v.key] = v.value;
        return result;
    };

    tab->onSave = [this, tab](const HttpRequest& req, const std::string& suggested) {
        if (tab->GetSavedId() != 0) {
            // update existing saved request in-place
            m_db->updateRequest(tab->GetSavedId(), req);
            RebuildTree();
            tab->SetDirty(false);
        } else {
            // save new — show name dialog
            int64_t newId = SaveRequest(req, suggested);
            if (newId != 0) {
                tab->SetSavedId(newId);
                tab->SetDirty(false);
            }
        }
    };
    tab->onRequestComplete = [this](const HttpRequest& req) { AddToHistory(req); };

    m_notebook->AddPage(tab, name, true);
    return tab;
}

void MainFrame::OpenInTab(const HttpRequest& req, const std::string& name, int64_t savedId) {
    // if savedId is set, switch to the existing tab for that request
    if (savedId != 0) {
        for (size_t i = 0; i < m_notebook->GetPageCount(); i++) {
            if (auto* t = dynamic_cast<RequestTab*>(m_notebook->GetPage(i))) {
                if (t->GetSavedId() == savedId) {
                    m_notebook->SetSelection(i);
                    return;
                }
            }
        }
    }
    auto* tab = NewTab(name);
    tab->SetSavedId(savedId);
    tab->LoadRequest(req);
}

void MainFrame::OpenInCollectionTab(int64_t collId, const std::string& name) {
    // check for existing tab
    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
        if (auto* t = dynamic_cast<CollectionTab*>(m_notebook->GetPage(i))) {
            if (t->GetCollectionId() == collId) {
                m_notebook->SetSelection(i);
                return;
            }
        }
    }
    m_rightBook->SetSelection(1);
    auto* tab = new CollectionTab(m_notebook, collId, name, m_db.get());
    m_notebook->AddPage(tab, name, true);
}

// ── history
// ───────────────────────────────────────────────────────────────────

void MainFrame::AddToHistory(const HttpRequest& req) {
    std::string url = req.url;
    if (url.starts_with("https://"))
        url = url.substr(8);
    else if (url.starts_with("http://"))
        url = url.substr(7);
    if (url.size() > 38)
        url = url.substr(0, 35) + "...";

    m_history.insert(m_history.begin(), {req, req.method + "  " + url});
    m_historyList->Insert(m_history.front().label, 0);
}

void MainFrame::OnHistorySelect(wxCommandEvent&) {
    int sel = m_historyList->GetSelection();
    if (sel != wxNOT_FOUND && sel < (int)m_history.size())
        OpenInTab(m_history[sel].request, m_history[sel].label);
}

void MainFrame::OnClearHistory(wxCommandEvent&) {
    m_history.clear();
    m_historyList->Clear();
}

void MainFrame::OnNewTab(wxCommandEvent&) {
    NewTab();
}

// ── collections tree
// ──────────────────────────────────────────────────────────

void MainFrame::RebuildTree() {
    m_tree->DeleteAllItems();
    m_treeRoot = m_tree->AddRoot("root");

    for (auto& coll : m_db->getCollections()) {
        auto item = m_tree->AppendItem(m_treeRoot, coll.name, 0);
        m_tree->SetItemImage(item, 1, wxTreeItemIcon_Expanded);
        m_tree->SetItemBold(item, true);
        m_tree->SetItemData(item,
                            new CollectionItemData(CollectionItemData::Type::Collection, coll.id));
        BuildTreeBranch(item, coll.id, 0);
    }
    m_tree->ExpandAll();
}

void MainFrame::BuildTreeBranch(wxTreeItemId parent, int64_t collectionId, int64_t parentFolderId) {
    for (auto& f : m_db->getFolders(collectionId, parentFolderId)) {
        auto item = m_tree->AppendItem(parent, f.name, 0);
        m_tree->SetItemImage(item, 1, wxTreeItemIcon_Expanded);
        m_tree->SetItemData(item, new CollectionItemData(CollectionItemData::Type::Folder, f.id));
        BuildTreeBranch(item, collectionId, f.id);
    }
    for (auto& r : m_db->getRequests(collectionId, parentFolderId)) {
        auto label = r.request.method + "  " + r.name;
        auto item = m_tree->AppendItem(parent, label, 2); // 2=file icon
        m_tree->SetItemTextColour(item, MethodColor(r.request.method));
        m_tree->SetItemData(item, new CollectionItemData(CollectionItemData::Type::Request, r.id));
    }
}

int64_t MainFrame::ContextCollectionId() const {
    auto sel = m_tree->GetSelection();
    if (!sel.IsOk())
        return 0;
    // walk up until we find a Collection item
    auto item = sel;
    while (item.IsOk() && item != m_treeRoot) {
        auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(item));
        if (d && d->type == CollectionItemData::Type::Collection)
            return d->id;
        item = m_tree->GetItemParent(item);
    }
    return 0;
}

int64_t MainFrame::ContextFolderId() const {
    auto sel = m_tree->GetSelection();
    if (!sel.IsOk())
        return 0;
    auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(sel));
    if (!d)
        return 0;
    if (d->type == CollectionItemData::Type::Collection)
        return 0;
    if (d->type == CollectionItemData::Type::Folder)
        return d->id;
    return m_db->getRequest(d->id).folderId;
}

int64_t MainFrame::GetOrAutoSelectCollection() {
    int64_t collId = ContextCollectionId();
    if (collId == 0) {
        auto colls = m_db->getCollections();
        collId = colls.empty() ? m_db->createCollection("My Collection") : colls.front().id;
    }
    return collId;
}

int64_t MainFrame::SaveRequest(const HttpRequest& req, const std::string& suggestedName) {
    wxString defaultName = suggestedName.empty() ? wxString(req.url).AfterLast('/').BeforeFirst('?')
                                                 : wxString(suggestedName);
    if (defaultName.IsEmpty())
        defaultName = req.url;

    wxString name =
        wxGetTextFromUser("Name for this request:", "Save to Collection", defaultName, this);
    if (name.IsEmpty())
        return 0;

    int64_t id =
        m_db->saveRequest(name.ToStdString(), GetOrAutoSelectCollection(), ContextFolderId(), req);
    RebuildTree();
    m_sidebarTabs->SetSelection(0);
    return id;
}

// ── tree events
// ───────────────────────────────────────────────────────────────

void MainFrame::OnTreeActivated(wxTreeEvent& evt) {
    auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(evt.GetItem()));
    if (!d)
        return;
    if (d->type == CollectionItemData::Type::Collection) {
        std::string text = m_tree->GetItemText(evt.GetItem()).ToStdString();
        OpenInCollectionTab(d->id, text);
    } else if (d->type == CollectionItemData::Type::Request) {
        auto saved = m_db->getRequest(d->id);
        OpenInTab(saved.request, saved.name, saved.id);
    }
}

void MainFrame::OnTreeBeginDrag(wxTreeEvent& evt) {
    if (evt.GetItem() == m_treeRoot)
        return;
    m_dragItem = evt.GetItem();
    evt.Allow();
}

// returns true if `candidate` is the same as or a descendant of `ancestor`
static bool IsFolderDescendant(DBStore* db, int64_t collectionId, int64_t candidate,
                               int64_t ancestor) {
    if (candidate == ancestor)
        return true;
    for (auto& child : db->getFolders(collectionId, ancestor)) {
        if (IsFolderDescendant(db, collectionId, candidate, child.id))
            return true;
    }
    return false;
}

void MainFrame::OnTreeEndDrag(wxTreeEvent& evt) {
    if (!m_dragItem.IsOk())
        return;
    auto target = evt.GetItem();
    if (!target.IsOk() || target == m_dragItem) {
        m_dragItem = wxTreeItemId{};
        return;
    }

    auto* src = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(m_dragItem));
    auto* tgt = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(target));
    m_dragItem = wxTreeItemId{};
    if (!src)
        return;
    // don't allow dragging collections
    if (src->type == CollectionItemData::Type::Collection)
        return;

    int64_t newParent =
        tgt ? (tgt->type == CollectionItemData::Type::Folder ? tgt->id
                                                             : m_db->getRequest(tgt->id).folderId)
            : 0;

    // prevent creating a cycle by dragging a folder into one of its own descendants
    if (src->type == CollectionItemData::Type::Folder) {
        int64_t collId = ContextCollectionId();
        if (IsFolderDescendant(m_db.get(), collId, newParent, src->id))
            return;
    }

    if (src->type == CollectionItemData::Type::Folder)
        m_db->moveFolder(src->id, newParent);
    else
        m_db->moveRequest(src->id, newParent);

    RebuildTree();
}

void MainFrame::OnTreeEndLabelEdit(wxTreeEvent& evt) {
    if (evt.IsEditCancelled())
        return;
    auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(evt.GetItem()));
    if (!d)
        return;
    std::string newName = evt.GetLabel().ToStdString();
    if (d->type == CollectionItemData::Type::Collection) {
        m_db->renameCollection(d->id, newName);
    } else if (d->type == CollectionItemData::Type::Folder) {
        m_db->renameFolder(d->id, newName);
    } else {
        // tree label is "METHOD  name"; strip the prefix so we only store the name
        std::string method = m_db->getRequest(d->id).request.method;
        std::string prefix = method + "  ";
        if (newName.starts_with(prefix))
            newName = newName.substr(prefix.size());
        m_db->renameRequest(d->id, newName);
        CallAfter([this]() { RebuildTree(); });
        evt.Veto();
    }
}

void MainFrame::OnTreeContextMenu(wxContextMenuEvent& evt) {
    // resolve which item (if any) was right-clicked
    wxTreeItemId item;
    wxPoint screenPos = evt.GetPosition();
    if (screenPos != wxDefaultPosition) {
        int flags = 0;
        item = m_tree->HitTest(m_tree->ScreenToClient(screenPos), flags);
        if (!(flags & (wxTREE_HITTEST_ONITEMLABEL | wxTREE_HITTEST_ONITEM |
                       wxTREE_HITTEST_ONITEMINDENT | wxTREE_HITTEST_ONITEMBUTTON)))
            item = wxTreeItemId{};
    } else {
        item = m_tree->GetSelection();
    }

    if (!item.IsOk()) {
        // background click — only new collection
        wxMenu menu;
        menu.Append(ID_TREE_NEW_COLLECTION, "New Collection");
        menu.Bind(wxEVT_MENU, &MainFrame::OnMenuNewCollection, this, ID_TREE_NEW_COLLECTION);
        PopupMenu(&menu);
        return;
    }

    m_tree->SelectItem(item);
    auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(item));

    wxMenu menu;
    if (d && d->type == CollectionItemData::Type::Collection) {
        menu.Append(ID_TREE_OPEN_COLLECTION, "Open");
        menu.AppendSeparator();
        menu.Append(ID_TREE_NEW_FOLDER, "New Folder");
        menu.Append(ID_TREE_NEW_REQUEST, "New Request");
        menu.AppendSeparator();
        menu.Append(ID_TREE_RENAME, "Rename");
        menu.Append(ID_TREE_DELETE, "Delete");
    } else if (!d || d->type == CollectionItemData::Type::Folder) {
        menu.Append(ID_TREE_NEW_REQUEST, "New Request");
        menu.Append(ID_TREE_NEW_FOLDER, "New Folder");
        if (d) {
            menu.AppendSeparator();
            menu.Append(ID_TREE_RENAME, "Rename");
            menu.Append(ID_TREE_DELETE, "Delete");
        }
    } else {
        menu.Append(ID_TREE_LOAD, "Open in Tab");
        menu.AppendSeparator();
        menu.Append(ID_TREE_RENAME, "Rename");
        menu.Append(ID_TREE_DELETE, "Delete");
    }

    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuNewCollection, this, ID_TREE_NEW_COLLECTION);
    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuNewRequest, this, ID_TREE_NEW_REQUEST);
    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuNewFolder, this, ID_TREE_NEW_FOLDER);
    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuRename, this, ID_TREE_RENAME);
    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuDelete, this, ID_TREE_DELETE);
    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuLoad, this, ID_TREE_LOAD);
    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuOpenCollection, this, ID_TREE_OPEN_COLLECTION);
    PopupMenu(&menu);
}

void MainFrame::OnMenuNewCollection(wxCommandEvent&) {
    wxString name = wxGetTextFromUser("Collection name:", "New Collection", "New Collection", this);
    if (name.IsEmpty())
        return;
    m_db->createCollection(name.ToStdString());
    RebuildTree();
}

void MainFrame::OnMenuNewRequest(wxCommandEvent&) {
    wxString name = wxGetTextFromUser("Request name:", "New Request", "New Request", this);
    if (name.IsEmpty())
        return;
    HttpRequest blank;
    blank.method = "GET";
    blank.url = "https://";
    m_db->saveRequest(name.ToStdString(), GetOrAutoSelectCollection(), ContextFolderId(), blank);
    RebuildTree();
}

void MainFrame::OnMenuNewFolder(wxCommandEvent&) {
    wxString name = wxGetTextFromUser("Folder name:", "New Folder", "New Folder", this);
    if (name.IsEmpty())
        return;
    m_db->createFolder(name.ToStdString(), GetOrAutoSelectCollection(), ContextFolderId());
    RebuildTree();
}

void MainFrame::OnMenuRename(wxCommandEvent&) {
    auto sel = m_tree->GetSelection();
    if (sel.IsOk())
        m_tree->EditLabel(sel);
}

void MainFrame::OnMenuDelete(wxCommandEvent&) {
    auto sel = m_tree->GetSelection();
    if (!sel.IsOk())
        return;
    auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(sel));
    if (!d)
        return;
    if (wxMessageBox("Delete this item?", "Confirm", wxYES_NO | wxICON_QUESTION, this) != wxYES)
        return;
    if (d->type == CollectionItemData::Type::Collection) {
        int64_t collId = d->id;
        // close any open CollectionTab for this collection
        for (size_t i = 0; i < m_notebook->GetPageCount();) {
            if (auto* t = dynamic_cast<CollectionTab*>(m_notebook->GetPage(i))) {
                if (t->GetCollectionId() == collId) {
                    m_notebook->DeletePage(i);
                    continue;
                }
            }
            ++i;
        }
        m_db->deleteCollection(collId);
    } else if (d->type == CollectionItemData::Type::Folder) {
        m_db->deleteFolder(d->id);
    } else {
        m_db->deleteRequest(d->id);
    }
    RebuildTree();
}

void MainFrame::OnMenuLoad(wxCommandEvent&) {
    auto sel = m_tree->GetSelection();
    if (!sel.IsOk())
        return;
    auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(sel));
    if (d && d->type == CollectionItemData::Type::Request) {
        auto saved = m_db->getRequest(d->id);
        OpenInTab(saved.request, saved.name, saved.id);
    }
}

void MainFrame::OnMenuOpenCollection(wxCommandEvent&) {
    auto sel = m_tree->GetSelection();
    if (!sel.IsOk())
        return;
    auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(sel));
    if (d && d->type == CollectionItemData::Type::Collection) {
        std::string text = m_tree->GetItemText(sel).ToStdString();
        OpenInCollectionTab(d->id, text);
    }
}

// ── import
// ───────────────────────────────────────────────────────────────────

void MainFrame::ImportSwagger(const std::string& path) {
    nlohmann::json doc;
    try {
        std::ifstream f(path);
        f >> doc;
    } catch (...) {
        wxMessageBox("Failed to parse JSON.", "Import Error", wxOK | wxICON_ERROR, this);
        return;
    }

    bool isV2 = doc.contains("swagger");
    bool isV3 = doc.contains("openapi");
    if ((!isV2 && !isV3) || !doc.contains("paths")) {
        wxMessageBox("Unrecognized format. Expected Swagger 2.0 or OpenAPI 3.x with paths.",
                     "Import Error", wxOK | wxICON_ERROR, this);
        return;
    }

    // base URL
    std::string baseUrl;
    if (isV2) {
        std::string host = doc.value("host", "");
        std::string basePath = doc.value("basePath", "");
        std::string scheme = "https";
        if (doc.contains("schemes") && !doc["schemes"].empty())
            scheme = doc["schemes"][0].get<std::string>();
        baseUrl = host.empty() ? basePath : (scheme + "://" + host + basePath);
    } else {
        if (doc.contains("servers") && !doc["servers"].empty())
            baseUrl = doc["servers"][0].value("url", "");
    }

    // collection name from info.title or filename
    std::string collName;
    if (doc.contains("info") && doc["info"].is_object())
        collName = doc["info"].value("title", "");
    if (collName.empty())
        collName = wxFileName(path).GetName().ToStdString();

    // create a collection for this import
    int64_t rootCollId = m_db->createCollection(collName);

    // store base URL as a collection variable so requests can use {{basePath}}
    if (!baseUrl.empty())
        m_db->setCollectionVariables(rootCollId,
                                     {CollectionVariable{"basePath", baseUrl, "base url"}});

    std::map<std::string, int64_t> tagFolders;
    auto tagFolder = [&](const std::string& tag) -> int64_t {
        auto [it, inserted] = tagFolders.emplace(tag, 0);
        if (inserted)
            it->second = m_db->createFolder(tag, rootCollId, 0);
        return it->second;
    };

    static const std::string kMethods[] = {"get",    "post", "put",    "patch",
                                           "delete", "head", "options"};
    int count = 0;

    for (auto& [pathStr, pathItem] : doc["paths"].items()) {
        // path-level parameters shared by all methods
        std::vector<nlohmann::json> pathParams;
        if (pathItem.contains("parameters") && pathItem["parameters"].is_array())
            pathParams = pathItem["parameters"].get<std::vector<nlohmann::json>>();

        for (auto& m : kMethods) {
            if (!pathItem.contains(m))
                continue;
            auto& op = pathItem[m];

            // merge path-level + operation-level parameters
            std::vector<nlohmann::json> params = pathParams;
            if (op.contains("parameters") && op["parameters"].is_array())
                for (auto& p : op["parameters"])
                    params.push_back(p);

            HttpRequest req;
            req.method = m;
            std::transform(req.method.begin(), req.method.end(), req.method.begin(), ::toupper);
            req.url = (baseUrl.empty() ? "" : "{{basePath}}") + pathStr;

            // query params → append as named placeholders; header params → empty header
            std::string qs;
            for (auto& p : params) {
                std::string in = p.value("in", "");
                std::string pname = p.value("name", "");
                if (pname.empty())
                    continue;
                if (in == "query")
                    qs += (qs.empty() ? "?" : "&") + pname + "=";
                else if (in == "header")
                    req.headers[pname] = "";
            }
            req.url += qs;

            // content-type from body parameters
            if (isV2) {
                bool hasBody = false, hasForm = false;
                for (auto& p : params) {
                    std::string in = p.value("in", "");
                    if (in == "body")
                        hasBody = true;
                    else if (in == "formData")
                        hasForm = true;
                }
                if (hasForm)
                    req.headers["Content-Type"] = "application/x-www-form-urlencoded";
                else if (hasBody)
                    req.headers["Content-Type"] = "application/json";
            } else if (op.contains("requestBody")) {
                auto& rb = op["requestBody"];
                if (rb.contains("content") && rb["content"].is_object()) {
                    auto& ct = rb["content"];
                    if (ct.contains("application/json"))
                        req.headers["Content-Type"] = "application/json";
                    else if (ct.contains("multipart/form-data"))
                        req.headers["Content-Type"] = "multipart/form-data";
                    else if (ct.contains("application/x-www-form-urlencoded"))
                        req.headers["Content-Type"] = "application/x-www-form-urlencoded";
                }
            }

            // request name: operationId > summary > METHOD /path
            std::string name;
            if (op.contains("operationId") && op["operationId"].is_string())
                name = op["operationId"].get<std::string>();
            else if (op.contains("summary") && op["summary"].is_string())
                name = op["summary"].get<std::string>();
            else
                name = req.method + " " + pathStr;

            // folder: first tag > root (folder_id=0 means directly under collection)
            int64_t folderId = 0;
            if (op.contains("tags") && op["tags"].is_array() && !op["tags"].empty())
                folderId = tagFolder(op["tags"][0].get<std::string>());

            m_db->saveRequest(name, rootCollId, folderId, req);
            ++count;
        }
    }

    RebuildTree();
    m_sidebarTabs->SetSelection(0);
    wxMessageBox(std::format("Imported {} requests into \"{}\".", count, collName),
                 "Import Complete", wxOK | wxICON_INFORMATION, this);
}
