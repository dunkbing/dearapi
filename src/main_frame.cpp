#include "main_frame.hpp"
#include <wx/artprov.h>
#include <wx/aui/tabart.h>

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

void MainFrame::UpdateRightView() {
    m_rightBook->SetSelection(m_notebook->GetPageCount() > 0 ? 1 : 0);
}

RequestTab* MainFrame::NewTab(const std::string& name) {
    m_rightBook->SetSelection(1); // show notebook before adding page
    auto* tab = new RequestTab(m_notebook, name, m_gate);

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
    BuildTreeBranch(m_treeRoot, 0);
    m_tree->ExpandAll();
}

void MainFrame::BuildTreeBranch(wxTreeItemId parent, int64_t parentId) {
    for (auto& f : m_db->getFolders(parentId)) {
        // 0=folder-closed, 1=folder-open (expanded state)
        auto item = m_tree->AppendItem(parent, f.name, 0);
        m_tree->SetItemImage(item, 1, wxTreeItemIcon_Expanded);
        m_tree->SetItemData(item, new CollectionItemData(CollectionItemData::Type::Folder, f.id));
        BuildTreeBranch(item, f.id);
    }
    for (auto& r : m_db->getRequests(parentId)) {
        auto label = r.request.method + "  " + r.name;
        auto item = m_tree->AppendItem(parent, label, 2); // 2=file icon
        m_tree->SetItemTextColour(item, MethodColor(r.request.method));
        m_tree->SetItemData(item, new CollectionItemData(CollectionItemData::Type::Request, r.id));
    }
}

int64_t MainFrame::ContextFolderId() const {
    auto sel = m_tree->GetSelection();
    if (!sel.IsOk())
        return 0;
    auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(sel));
    if (!d)
        return 0;
    if (d->type == CollectionItemData::Type::Folder)
        return d->id;
    return m_db->getRequest(d->id).folderId;
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

    int64_t id = m_db->saveRequest(name.ToStdString(), ContextFolderId(), req);
    RebuildTree();
    m_sidebarTabs->SetSelection(0);
    return id;
}

// ── tree events
// ───────────────────────────────────────────────────────────────

void MainFrame::OnTreeActivated(wxTreeEvent& evt) {
    auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(evt.GetItem()));
    if (d && d->type == CollectionItemData::Type::Request) {
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
static bool IsFolderDescendant(DBStore* db, int64_t candidate, int64_t ancestor) {
    if (candidate == ancestor)
        return true;
    for (auto& child : db->getFolders(ancestor)) {
        if (IsFolderDescendant(db, candidate, child.id))
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

    int64_t newParent =
        tgt ? (tgt->type == CollectionItemData::Type::Folder ? tgt->id
                                                             : m_db->getRequest(tgt->id).folderId)
            : 0;

    // prevent creating a cycle by dragging a folder into one of its own descendants
    if (src->type == CollectionItemData::Type::Folder &&
        IsFolderDescendant(m_db.get(), newParent, src->id))
        return;

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
    if (d->type == CollectionItemData::Type::Folder) {
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
        // background click — create at root (folderId = 0)
        wxMenu menu;
        menu.Append(ID_TREE_NEW_REQUEST, "New Request");
        menu.Append(ID_TREE_NEW_FOLDER, "New Folder");
        menu.Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                wxString name =
                    wxGetTextFromUser("Request name:", "New Request", "New Request", this);
                if (name.IsEmpty())
                    return;
                HttpRequest blank{"GET", "https://"};
                m_db->saveRequest(name.ToStdString(), 0, blank);
                RebuildTree();
            },
            ID_TREE_NEW_REQUEST);
        menu.Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                wxString name = wxGetTextFromUser("Folder name:", "New Folder", "New Folder", this);
                if (name.IsEmpty())
                    return;
                m_db->createFolder(name.ToStdString(), 0);
                RebuildTree();
            },
            ID_TREE_NEW_FOLDER);
        PopupMenu(&menu);
        return;
    }

    // item click — standard context menu
    m_tree->SelectItem(item);
    auto* d = dynamic_cast<CollectionItemData*>(m_tree->GetItemData(item));

    wxMenu menu;
    if (!d || d->type == CollectionItemData::Type::Folder) {
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

    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuNewRequest, this, ID_TREE_NEW_REQUEST);
    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuNewFolder, this, ID_TREE_NEW_FOLDER);
    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuRename, this, ID_TREE_RENAME);
    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuDelete, this, ID_TREE_DELETE);
    menu.Bind(wxEVT_MENU, &MainFrame::OnMenuLoad, this, ID_TREE_LOAD);
    PopupMenu(&menu);
}

void MainFrame::OnMenuNewRequest(wxCommandEvent&) {
    wxString name = wxGetTextFromUser("Request name:", "New Request", "New Request", this);
    if (name.IsEmpty())
        return;
    HttpRequest blank;
    blank.method = "GET";
    blank.url = "https://";
    m_db->saveRequest(name.ToStdString(), ContextFolderId(), blank);
    RebuildTree();
}

void MainFrame::OnMenuNewFolder(wxCommandEvent&) {
    wxString name = wxGetTextFromUser("Folder name:", "New Folder", "New Folder", this);
    if (name.IsEmpty())
        return;
    m_db->createFolder(name.ToStdString(), ContextFolderId());
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
    if (d->type == CollectionItemData::Type::Folder)
        m_db->deleteFolder(d->id);
    else
        m_db->deleteRequest(d->id);
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
