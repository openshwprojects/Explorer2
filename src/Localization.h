#pragma once
#include <windows.h>

namespace Explorer2 {

enum class Lang {
    PL = 0,
    EN = 1
};

struct Strings {
    // Ribbon Tabs
    const wchar_t* tabFile;
    const wchar_t* tabHome;
    const wchar_t* tabShare;
    const wchar_t* tabView;
    const wchar_t* tabSearch;

    // Home Ribbon Groups
    const wchar_t* grpClipboard;
    const wchar_t* grpOrganize;
    const wchar_t* grpNew;
    const wchar_t* grpOpen;
    const wchar_t* grpSelect;

    // Home Ribbon Buttons
    const wchar_t* btnPinQuick;
    const wchar_t* btnCopy;
    const wchar_t* btnPaste;
    const wchar_t* btnCut;
    const wchar_t* btnCopyPath;
    const wchar_t* btnPasteShortcut;
    const wchar_t* btnMoveTo;
    const wchar_t* btnCopyTo;
    const wchar_t* btnDelete;
    const wchar_t* btnRename;
    const wchar_t* btnNewFolder;
    const wchar_t* btnNewItem;
    const wchar_t* btnEasyAccess;
    const wchar_t* btnProperties;
    const wchar_t* btnOpen;
    const wchar_t* btnEdit;
    const wchar_t* btnHistory;
    const wchar_t* btnSelectAll;
    const wchar_t* btnSelectNone;
    const wchar_t* btnInvertSelection;

    // Delete Dropdown Menu
    const wchar_t* menuDeleteRecycle;
    const wchar_t* menuDeletePerm;
    const wchar_t* menuDeleteConfirmToggle;

    // View Ribbon Buttons & Modes
    const wchar_t* grpPanes;
    const wchar_t* grpLayout;
    const wchar_t* grpCurrentView;
    const wchar_t* grpShowHide;
    const wchar_t* btnNavPane;
    const wchar_t* btnPreviewPane;
    const wchar_t* viewJumbo;
    const wchar_t* viewExtraLarge;
    const wchar_t* viewLarge;
    const wchar_t* viewMedium;
    const wchar_t* viewSmall;
    const wchar_t* viewList;
    const wchar_t* viewDetails;
    const wchar_t* viewProportions;
    const wchar_t* viewFullViewer;
    const wchar_t* viewFullScreen;
    const wchar_t* chkFileExt;
    const wchar_t* chkHiddenItems;

    // Navigation & Left Tree
    const wchar_t* navQuickAccess;
    const wchar_t* navThisPC;
    const wchar_t* navNetwork;
    const wchar_t* navDesktop;
    const wchar_t* navDownloads;
    const wchar_t* navDocuments;
    const wchar_t* navPictures;
    const wchar_t* searchPlaceholder;

    // Details Columns
    const wchar_t* colName;
    const wchar_t* colDateModified;
    const wchar_t* colType;
    const wchar_t* colSize;
    const wchar_t* colActions;
    const wchar_t* btnRowTrash;
    const wchar_t* btnRowPermDel;

    // File Types
    const wchar_t* typeFolder;
    const wchar_t* typeFile;
    const wchar_t* typeImage;

    // Status Bar
    const wchar_t* statusItems;
    const wchar_t* statusSelected;
    const wchar_t* statusDeleting;
    const wchar_t* statusDeletedRecycle;
    const wchar_t* statusDeletedPerm;
    const wchar_t* statusRestored;

    // Context Menu (Item & Background)
    const wchar_t* ctxOpen;
    const wchar_t* ctxOpenNewWindow;
    const wchar_t* ctxPinQuickAccess;
    const wchar_t* ctxUnpinQuickAccess;
    const wchar_t* ctxExpand;
    const wchar_t* ctxCollapse;
    const wchar_t* ctxFullViewer;
    const wchar_t* ctxFullScreen;
    const wchar_t* ctxEdit;
    const wchar_t* ctxSetWallpaper;
    const wchar_t* ctxRotateRight;
    const wchar_t* ctxRotateLeft;
    const wchar_t* ctxOpenWith;
    const wchar_t* ctxChooseAnotherApp;
    const wchar_t* ctxSendTo;
    const wchar_t* ctxOpenExplorer;
    const wchar_t* ctxOpenTerminal;
    const wchar_t* ctxCut;
    const wchar_t* ctxCopy;
    const wchar_t* ctxPaste;
    const wchar_t* ctxPasteShortcut;
    const wchar_t* ctxCopyPath;
    const wchar_t* ctxCreateShortcut;
    const wchar_t* ctxDeleteRecycle;
    const wchar_t* ctxDeletePerm;
    const wchar_t* ctxRename;
    const wchar_t* ctxNew;
    const wchar_t* ctxNewFolder;
    const wchar_t* ctxNewTxt;
    const wchar_t* ctxNewBmp;
    const wchar_t* ctxNewPy;
    const wchar_t* ctxNewMd;
    const wchar_t* ctxProperties;
    const wchar_t* ctxRefresh;
    const wchar_t* ctxViewSubmenu;
    const wchar_t* ctxSortBySubmenu;
    const wchar_t* ctxGroupBySubmenu;
    const wchar_t* ctxSortAsc;
    const wchar_t* ctxSortDesc;
    const wchar_t* ctxGroupNone;
    const wchar_t* ctxUndoDelete;
    const wchar_t* ctxNativeShellMenu;

    // Dialogs & Viewer Overlay
    const wchar_t* dlgConfirmDeleteTitle;
    const wchar_t* dlgConfirmRecycleMsg;
    const wchar_t* dlgConfirmPermMsg;
    const wchar_t* dlgRenameTitle;
    const wchar_t* dlgNewFolderDefault;
    const wchar_t* viewerHelpHint;
    const wchar_t* viewerNoImages;
};

inline const Strings kStringsPL = {
    L"Plik", L"Narzędzia główne", L"Udostępnianie", L"Widok", L"Wyszukiwanie",
    L"Schowek", L"Organizowanie", L"Nowy", L"Otwieranie", L"Zaznaczanie",
    L"Przypnij do paska\nSzybki dostęp", L"Kopiuj", L"Wklej", L"Wytnij", L"Kopiuj ścieżkę", L"Wklej skrót",
    L"Przenieś\ndo", L"Kopiuj\ndo", L"Usuń", L"Zmień\nnazwę",
    L"Nowy\nfolder", L"Nowy element", L"Łatwy dostęp",
    L"Właściwości", L"Otwórz", L"Edytuj", L"Historia",
    L"Zaznacz wszystko", L"Nie zaznaczaj nic", L"Odwróć zaznaczenie",

    L"Przenieś do Kosza (Del)",
    L"Trwale usuń (Shift+Del)",
    L"Pokaż potwierdzenie usunięcia",

    L"Okienka", L"Układ", L"Widok bieżący", L"Pokazywanie/ukrywanie",
    L"Okienko\nnawigacji", L"Okienko\npodglądu",
    L"Gigantyczne ikony", L"Bardzo duże ikony", L"Duże ikony", L"Średnie ikony", L"Małe ikony",
    L"Lista", L"Szczegóły", L"Proporcje", L"Pełny podgląd (Viewer)", L"Pełny ekran (F11)",
    L"Rozszerzenia nazw plików", L"Ukryte elementy",

    L"Szybki dostęp", L"Ten komputer", L"Sieć",
    L"Pulpit", L"Pobrane", L"Dokumenty", L"Obrazy",
    L"Przeszukaj: ",

    L"Nazwa", L"Data modyfikacji", L"Typ", L"Rozmiar", L"Operacje", L"Kosz", L"Usuń",
    L"Folder plików", L"Plik", L"Obraz",

    L"Elementy: ", L"Zaznaczono: ", L"Usuwanie w tle...",
    L"Przeniesiono do Kosza (Ctrl+Z aby cofnąć)", L"Trwale usunięto", L"Przywrócono usunięty plik",

    L"Otwórz",
    L"Otwórz w nowym oknie",
    L"Przypnij do paska Szybki dostęp",
    L"Odepnij od paska Szybki dostęp",
    L"Rozwiń",
    L"Zwiń",
    L"Otwórz w Pełnym podglądzie (Space)",
    L"Pełny ekran (F11)",
    L"Edytuj",
    L"Ustaw jako tło pulpitu",
    L"Obróć w prawo",
    L"Obróć w lewo",
    L"Otwórz za pomocą",
    L"Wybierz inną aplikację...",
    L"Wyślij do",
    L"Pokaż w klasycznym Eksploratorze",
    L"Otwórz tutaj okno PowerShell / Terminal",
    L"Wytnij",
    L"Kopiuj",
    L"Wklej",
    L"Wklej skrót",
    L"Kopiuj jako ścieżkę",
    L"Utwórz skrót",
    L"Usuń",
    L"Trwale usuń (Shift+Del)",
    L"Zmień nazwę",
    L"Nowy",
    L"Folder",
    L"Dokument tekstowy (.txt)",
    L"Obraz mapy bitowej (.bmp)",
    L"Skrypt Python (.py)",
    L"Dokument Markdown (.md)",
    L"Właściwości",
    L"Odśwież",
    L"Widok",
    L"Sortuj według",
    L"Grupuj według",
    L"Rosnąco",
    L"Malejąco",
    L"(Brak)",
    L"Cofnij usunięcie (Ctrl+Z)",
    L"Pokaż pełne menu systemowe Windows Shell...",

    L"Usuwanie elementu",
    L"Czy na pewno chcesz przenieść wybrane elementy do Kosza?",
    L"Czy na pewno chcesz TRWALE usunąć wybrane elementy?",
    L"Zmień nazwę",
    L"Nowy folder",
    L"[←/→] Nawigacja   [Del] Kosz   [Shift+Del] Trwale usuń   [Scroll] Zoom   [1] 100%   [0] Dopasuj   [F11] Pełny ekran   [Esc/Space] Wróć",
    L"Brak obrazów w tym folderze (wybierz obraz lub przełącz widok)"
};

inline const Strings kStringsEN = {
    L"File", L"Home", L"Share", L"View", L"Search",
    L"Clipboard", L"Organize", L"New", L"Open", L"Select",
    L"Pin to\nQuick access", L"Copy", L"Paste", L"Cut", L"Copy path", L"Paste shortcut",
    L"Move\nto", L"Copy\nto", L"Delete", L"Rename",
    L"New\nfolder", L"New item", L"Easy access",
    L"Properties", L"Open", L"Edit", L"History",
    L"Select all", L"Select none", L"Invert selection",

    L"Recycle (Del)",
    L"Permanently delete (Shift+Del)",
    L"Show delete confirmation",

    L"Panes", L"Layout", L"Current view", L"Show/hide",
    L"Navigation\npane", L"Preview\npane",
    L"Jumbo icons", L"Extra large icons", L"Large icons", L"Medium icons", L"Small icons",
    L"List", L"Details", L"Proportions", L"Full Viewer (In-Window)", L"Full Screen (F11)",
    L"File name extensions", L"Hidden items",

    L"Quick access", L"This PC", L"Network",
    L"Desktop", L"Downloads", L"Documents", L"Pictures",
    L"Search: ",

    L"Name", L"Date modified", L"Type", L"Size", L"Actions", L"Bin", L"Purge",
    L"File folder", L"File", L"Image",

    L"Items: ", L"Selected: ", L"Deleting in background...",
    L"Moved to Recycle Bin (Ctrl+Z to undo)", L"Permanently deleted", L"Restored deleted item",

    L"Open",
    L"Open in new window",
    L"Pin to Quick access",
    L"Unpin from Quick access",
    L"Expand",
    L"Collapse",
    L"Open in Full Viewer (Space)",
    L"Full Screen (F11)",
    L"Edit",
    L"Set as desktop background",
    L"Rotate right",
    L"Rotate left",
    L"Open with",
    L"Choose another app...",
    L"Send to",
    L"Reveal in classic Explorer",
    L"Open PowerShell / Terminal here",
    L"Cut",
    L"Copy",
    L"Paste",
    L"Paste shortcut",
    L"Copy as path",
    L"Create shortcut",
    L"Delete",
    L"Permanently delete (Shift+Del)",
    L"Rename",
    L"New",
    L"Folder",
    L"Text Document (.txt)",
    L"Bitmap Image (.bmp)",
    L"Python Script (.py)",
    L"Markdown Document (.md)",
    L"Properties",
    L"Refresh",
    L"View",
    L"Sort by",
    L"Group by",
    L"Ascending",
    L"Descending",
    L"(None)",
    L"Undo Delete (Ctrl+Z)",
    L"Show full native Windows Shell menu...",

    L"Delete Item",
    L"Are you sure you want to move the selected items to the Recycle Bin?",
    L"Are you sure you want to PERMANENTLY delete the selected items?",
    L"Rename",
    L"New folder",
    L"[←/→] Prev/Next   [Del] Recycle   [Shift+Del] Perm Delete   [Scroll] Zoom   [1] 100%   [0] Fit   [F11] Fullscreen   [Esc/Space] Back",
    L"No images in this folder (select an image or switch view mode)"
};

inline const Strings& GetStrings(Lang lang) {
    return (lang == Lang::PL) ? kStringsPL : kStringsEN;
}

} // namespace Explorer2
