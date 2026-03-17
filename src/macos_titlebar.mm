#include "macos_titlebar.hpp"
#include "main_frame.hpp"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

static NSToolbarItemIdentifier const kToggleSidebarID = @"ToggleSidebar";
static NSToolbarItemIdentifier const kNewTabID = @"NewTab";
static NSToolbarItemIdentifier const kImportID = @"Import";

// ── toolbar delegate ──────────────────────────────────────────────────────────

@interface DearAPIToolbarDelegate : NSObject <NSToolbarDelegate>
@property(nonatomic, assign) MainFrame* frame;
@end

@implementation DearAPIToolbarDelegate

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar
        itemForItemIdentifier:(NSToolbarItemIdentifier)ident
    willBeInsertedIntoToolbar:(BOOL)flag {
    NSToolbarItem* item = [[NSToolbarItem alloc] initWithItemIdentifier:ident];

    if ([ident isEqualToString:kToggleSidebarID]) {
        item.label = @"Toggle Sidebar";
        item.paletteLabel = @"Toggle Sidebar";
        item.toolTip = @"Toggle sidebar";
        if (@available(macOS 11.0, *))
            item.image = [NSImage imageWithSystemSymbolName:@"sidebar.left"
                                   accessibilityDescription:@"Toggle Sidebar"];
        else
            item.image = [NSImage imageNamed:NSImageNameTouchBarSidebarTemplate];
        item.target = self;
        item.action = @selector(toggleSidebar:);
    } else if ([ident isEqualToString:kNewTabID]) {
        item.label = @"New Tab";
        item.paletteLabel = @"New Tab";
        item.toolTip = @"New tab";
        if (@available(macOS 11.0, *))
            item.image = [NSImage imageWithSystemSymbolName:@"plus"
                                   accessibilityDescription:@"New Tab"];
        else
            item.image = [NSImage imageNamed:NSImageNameAddTemplate];
        item.target = self;
        item.action = @selector(newTab:);
    } else if ([ident isEqualToString:kImportID]) {
        item.label = @"Import";
        item.paletteLabel = @"Import";
        item.toolTip = @"Import collection";
        if (@available(macOS 11.0, *))
            item.image = [NSImage imageWithSystemSymbolName:@"arrow.down.doc"
                                   accessibilityDescription:@"Import"];
        else
            item.image = [NSImage imageNamed:NSImageNameFolder];
        item.target = self;
        item.action = @selector(importCollection:);
    }

    return item;
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar {
    return @[ kToggleSidebarID, kNewTabID, kImportID, NSToolbarFlexibleSpaceItemIdentifier ];
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar {
    return @[
        kToggleSidebarID, kNewTabID, kImportID, NSToolbarFlexibleSpaceItemIdentifier,
        NSToolbarSpaceItemIdentifier
    ];
}

- (void)toggleSidebar:(id)sender {
    _frame->ToggleSidebar();
}
- (void)newTab:(id)sender {
    _frame->DoNewTab();
}
- (void)importCollection:(id)sender {
    _frame->DoImport();
}

@end

// ── public entry point ────────────────────────────────────────────────────────

void SetupMacOSTitlebar(MainFrame* frame) {
    // wxFrame::GetHandle() returns the content NSView*, not the NSWindow*
    NSView* contentView = (NSView*)frame->GetHandle();
    NSWindow* window = contentView.window;
    if (!window)
        return;

    DearAPIToolbarDelegate* delegate = [[DearAPIToolbarDelegate alloc] init];
    delegate.frame = frame;

    // keep the delegate alive for the lifetime of the window
    static const char kDelegateKey = 0;
    objc_setAssociatedObject(window, &kDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"DearAPIToolbar"];
    toolbar.delegate = delegate;
    toolbar.displayMode = NSToolbarDisplayModeIconOnly;

    window.toolbar = toolbar;
    window.titleVisibility = NSWindowTitleHidden;

    // merge toolbar into titlebar (Big Sur+); older macOS just shows it below
    if (@available(macOS 11.0, *))
        window.toolbarStyle = NSWindowToolbarStyleUnified;
}
