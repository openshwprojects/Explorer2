#pragma once

namespace Explorer2 {

enum CommandId : int {
    CMD_NONE = 0,

    // Ribbon Tabs & Chrome
    CMD_TAB_FILE = 1,
    CMD_TAB_HOME = 2,
    CMD_TAB_SHARE = 3,
    CMD_TAB_VIEW = 4,
    CMD_TOGGLE_LANG = 5,
    CMD_TOGGLE_RIBBON = 6,
    CMD_TAB_SEARCH = 7,
    CMD_TAB_HARDWARE = 8,

    // Home Tab Commands
    CMD_PIN_QUICK = 10,
    CMD_COPY = 11,
    CMD_PASTE = 12,
    CMD_CUT = 13,
    CMD_COPY_PATH = 14,
    CMD_PASTE_SHORTCUT = 15,
    CMD_MOVE_TO = 16,
    CMD_COPY_TO = 17,
    CMD_DELETE_MAIN = 18,      // "Kosz" big button (move to Recycle Bin)
    CMD_DELETE_DROPDOWN = 19,  // Bottom "Usuń v" arrow (Menu: Recycle / Permanent / Confirm)
    CMD_RENAME = 20,
    CMD_NEW_FOLDER = 21,
    CMD_NEW_ITEM = 22,
    CMD_PROPERTIES = 23,
    CMD_OPEN = 24,
    CMD_EDIT = 25,
    CMD_SELECT_ALL = 26,
    CMD_SELECT_NONE = 27,
    CMD_INVERT_SEL = 28,
    CMD_DELETE_PERM = 29,      // Top half of split "Usuń" button (permanent delete)

    // View Tab & Status Bar Mode Switchers
    CMD_TOGGLE_NAVPANE = 40,
    CMD_TOGGLE_PREVIEWPANE = 41,
    CMD_VIEW_EXTRALARGE = 42,
    CMD_VIEW_LARGE = 43,
    CMD_VIEW_MEDIUM = 44,
    CMD_VIEW_SMALL = 45,
    CMD_VIEW_LIST = 46,
    CMD_VIEW_DETAILS = 47,
    CMD_VIEW_FULLVIEWER = 48,
    CMD_VIEW_FULLSCREEN = 49,
    CMD_TOGGLE_EXT = 50,
    CMD_TOGGLE_HIDDEN = 51,
    CMD_VIEW_JUMBO = 52,
    CMD_VIEW_PROPORTIONS = 53,
    CMD_VIEW_PIECHART = 54,

    // Navigation Bar & Column Header Buttons
    CMD_NAV_BACK = 60,
    CMD_NAV_FORWARD = 61,
    CMD_NAV_UP = 62,
    CMD_NAV_REFRESH = 63,
    CMD_CLEAR_SEARCH = 64,
    CMD_CALC_FOLDER_SIZES = 65,
    CMD_SEARCH_ADVANCED_GEAR = 66,
    CMD_SEARCH_TRIGGER_BTN = 67,

    // Full Viewer Canvas HUD Buttons
    CMD_VIEWER_PREV = 70,
    CMD_VIEWER_NEXT = 71,
    CMD_VIEWER_DELETE = 72,
    CMD_VIEWER_PERM_DELETE = 73,
    CMD_VIEWER_ZOOM_FIT = 74,
    CMD_VIEWER_ZOOM_100 = 75,
    CMD_VIEWER_FULLSCREEN = 76,
    CMD_VIEWER_CLOSE = 77,
    CMD_MEDIA_PLAY_PAUSE = 78,
    CMD_MEDIA_SEEK_BAR = 79,

    // Advanced Search ("Wyszukiwanie") Ribbon Tab Commands (80..125)
    CMD_SEARCH_START = 80,
    CMD_SEARCH_STOP = 81,
    CMD_SEARCH_RESET_FILTERS = 82,
    CMD_SEARCH_CHK_RECURSIVE = 83,
    CMD_SEARCH_CHK_FILES = 84,
    CMD_SEARCH_CHK_FOLDERS = 85,
    CMD_SEARCH_CHK_CASESENS = 86,
    CMD_SEARCH_CHK_CONTENT = 87,

    CMD_SEARCH_CHK_DATE = 90,
    CMD_SEARCH_EDIT_DATE_FROM = 91,
    CMD_SEARCH_EDIT_DATE_TO = 92,
    CMD_SEARCH_PRESET_TODAY = 93,
    CMD_SEARCH_PRESET_7DAYS = 94,
    CMD_SEARCH_PRESET_30DAYS = 95,
    CMD_SEARCH_PRESET_YEAR = 96,

    CMD_SEARCH_CHK_SIZE = 100,
    CMD_SEARCH_EDIT_SIZE_MIN = 101,
    CMD_SEARCH_EDIT_SIZE_MAX = 102,
    CMD_SEARCH_CYCLE_SIZE_UNIT = 103,
    CMD_SEARCH_PRESET_SMALL = 104,
    CMD_SEARCH_PRESET_GT10MB = 105,
    CMD_SEARCH_PRESET_GT100MB = 106,
    CMD_SEARCH_PRESET_GT1GB = 107,

    CMD_SEARCH_CHK_TYPE = 110,
    CMD_SEARCH_CAT_IMAGES = 111,
    CMD_SEARCH_CAT_MEDIA = 112,
    CMD_SEARCH_CAT_CODE = 113,
    CMD_SEARCH_CAT_ARCHIVES = 114,
    CMD_SEARCH_CAT_MODELS = 115,
    CMD_SEARCH_CAT_APPS = 116,
    CMD_SEARCH_CAT_DOCS = 117,
    CMD_SEARCH_TYPE_LIST_MENU = 118,
    CMD_SEARCH_EDIT_CUSTOM_EXT = 119,

    // Share ("Udostępnianie") & File ("Plik") Ribbon Tab Commands (120..135)
    CMD_SHARE_ZIP = 120,
    CMD_SHARE_EXTRACT_ZIP = 121,
    CMD_SHARE_EMAIL = 122,
    CMD_SHARE_PRINT = 123,
    CMD_SHARE_COPY_NAME = 124,
    CMD_SHARE_WIN_EXPLORER = 125,
    CMD_SHARE_POWERSHELL = 126,
    CMD_SHARE_CMD = 127,
    CMD_SHARE_MAP_DRIVE = 128,
    CMD_SHARE_DISCONNECT_DRIVE = 129,
    CMD_FILE_NEW_WINDOW = 130,
    CMD_FILE_EXIT = 131,

    // Hardware Pro Mode ("Sprzęt") Ribbon & Dashboard Commands (140..165)
    CMD_HW_SUB_ALL = 140,
    CMD_HW_SUB_CPU = 141,
    CMD_HW_SUB_GPU = 142,
    CMD_HW_SUB_RAM = 143,
    CMD_HW_SUB_MOBO = 144,
    CMD_HW_SUB_DISKS = 145,
    CMD_HW_SUB_NET = 146,
    CMD_HW_SUB_SYSTEM = 147,
    CMD_HW_REFRESH = 148,
    CMD_HW_TOGGLE_LIVE = 149,
    CMD_HW_BENCH_CPU = 150,
    CMD_HW_BENCH_RAM = 151,
    CMD_HW_COPY_REPORT = 152,
    CMD_HW_SAVE_REPORT = 153,
    CMD_HW_OPEN_DEVMGR = 154,
    CMD_HW_OPEN_TASKMGR = 155,
    CMD_HW_OPEN_MSINFO = 156,
    CMD_HW_OPEN_DXDIAG = 157,
    CMD_HW_BACK_TO_FILES = 158,
    CMD_HW_SUB_THERMAL = 159,
    CMD_HW_RELAUNCH_ADMIN = 160,
    CMD_HW_REFRESH_SMART = 161,
    CMD_HW_NET_SETTINGS = 162,   // card-hit only: opens the Windows adapter properties dialog
    CMD_HW_SELECT_DISK = 163,    // card-hit only: selects a physical disk for the SMART panel

    // "Programy" (installed programs) tab: add / remove programs
    CMD_TAB_PROGRAMS = 164,
    CMD_PROG_ADD = 165,            // pick an installer (.exe / .msi / .msix) and run it
    CMD_PROG_UNINSTALL = 166,      // uninstall the selected program
    CMD_PROG_REFRESH = 167,
    CMD_PROG_APPWIZ = 168,         // classic "Programs and Features"
    CMD_PROG_SETTINGS = 169,       // Windows Settings > Apps & features
    CMD_PROG_OPENLOC = 170,        // open install folder of the selected program
    CMD_PROG_SELECT = 171,         // card-hit only: row click (actionArg = registry key)
    CMD_PROG_UNINSTALL_ROW = 172,  // card-hit only: per-row uninstall button
    CMD_PROG_SORT = 173,           // card-hit only: column header (actionIndex = column)

    // "Monitoring" tab: background recorder + history graphs
    CMD_TAB_MONITORING = 174,
    CMD_MON_TOGGLE = 175,          // start / stop the background recorder
    CMD_MON_AUTOSTART = 176,       // start the recorder at Windows logon
    CMD_MON_RANGE = 177,           // card-hit only: time range chip (actionIndex = range)
    CMD_MON_INTERVAL = 178,        // cycle sampling interval
    CMD_MON_RETENTION = 179,       // cycle retention (days)
    CMD_MON_OPENDIR = 180,         // open the data folder in Explorer
    CMD_MON_REFRESH = 181,         // reload history now
    CMD_MON_ELEVATED = 182,        // run the recorder as administrator (SATA SMART)
    CMD_MON_ZOOM = 183,            // card-hit only: chart "fill window" (actionIndex = chart id, -1 = back)

    // "LAN" tab: network scanner + SSDP
    CMD_TAB_LAN = 184,
    CMD_LAN_SCAN = 185,            // start / stop host discovery
    CMD_LAN_SSDP = 186,            // SSDP (UPnP) discovery
    CMD_LAN_ADAPTER = 187,         // next adapter (rescans)
    CMD_LAN_SELECT = 188,          // card-hit only: select a host (actionArg = ip)
    CMD_LAN_PORTSCAN = 189,        // TCP port scan of the selected host
    CMD_LAN_OPEN_WEB = 190,        // open actionArg (URL) in the default browser
    CMD_LAN_OPEN_SMB = 191,        // open \\<actionArg ip> in Windows Explorer
    CMD_LAN_COPY = 192,            // copy actionArg to the clipboard

    // Git dock
    CMD_TOGGLE_GIT = 193,          // "Integracja GIT" checkbox
    CMD_TAB_LOG = 194,             // "Log" tab
    CMD_LOG_LEVEL = 195,           // cycle the minimum level filter (all / warnings+ / errors)
    CMD_LOG_CLEAR = 196,           // clear the on-screen list (the file is kept)
    CMD_LOG_OPEN_FOLDER = 197,     // open the Logs folder
    CMD_LOG_OPEN_FILE = 198,       // open this run's log file in the default editor
    CMD_LOG_COPY = 199,            // copy actionArg (a log line) / the whole visible log if empty

    CMD_BASE_SEARCH_EXT_MENU = 700, // 700..799

    // Context Menu / Dropdown IDs
    CMD_CTX_OPEN = 200,
    CMD_CTX_OPEN_NEW_WINDOW = 201,
    CMD_CTX_PIN_QUICK = 202,
    CMD_CTX_FULLVIEWER = 203,
    CMD_CTX_FULLSCREEN = 204,
    CMD_CTX_EDIT_IMAGE = 205,
    CMD_CTX_SET_WALLPAPER = 206,
    CMD_CTX_ROTATE_CW = 207,
    CMD_CTX_ROTATE_CCW = 208,
    CMD_CTX_EXPLORER = 209,
    CMD_CTX_TERMINAL = 210,
    CMD_CTX_CUT = 211,
    CMD_CTX_COPY = 212,
    CMD_CTX_PASTE = 213,
    CMD_CTX_PASTE_SHORTCUT = 214,
    CMD_CTX_COPY_PATH = 215,
    CMD_CTX_CREATE_SHORTCUT = 216,
    CMD_CTX_DELETE_RECYCLE = 217,
    CMD_CTX_DELETE_PERM = 218,
    CMD_CTX_TOGGLE_CONFIRM = 219,
    CMD_CTX_RENAME = 220,
    CMD_CTX_PROPERTIES = 221,
    CMD_CTX_REFRESH = 222,
    CMD_CTX_UNDO_DELETE = 223,
    CMD_CTX_NATIVE_SHELL_MENU = 224,
    CMD_CTX_OPEN_WITH_DIALOG = 225,
    CMD_CTX_UNPIN_QUICK = 226,
    CMD_CTX_EXPAND_COLLAPSE = 227,

    // Sort By Submenu
    CMD_SORT_NAME = 240,
    CMD_SORT_DATE = 241,
    CMD_SORT_TYPE = 242,
    CMD_SORT_SIZE = 243,
    CMD_SORT_ASC = 244,
    CMD_SORT_DESC = 245,

    // New Submenu
    CMD_NEW_FOLDER_CTX = 250,
    CMD_NEW_TXT_FILE = 251,
    CMD_NEW_BMP_FILE = 252,
    CMD_NEW_PY_FILE = 253,
    CMD_NEW_MD_FILE = 254,

    // Dynamic Range Bases for Emulated Submenus
    CMD_BASE_7ZIP = 300,          // 301..310
    CMD_BASE_OPENWITH = 320,      // 320..349
    CMD_BASE_SENDTO = 350,        // 350..399
    CMD_BASE_REG_VERBS = 400,     // 400..499
    CMD_BASE_BREADCRUMB = 500,    // 500..699

    // Split View (Dual-pane Explorer)
    CMD_TOGGLE_SPLIT_VIEW = 800,
    CMD_SPLIT_SWAP_PANES  = 801,
    CMD_SPLIT_COPY_OTHER  = 802,
    CMD_SPLIT_MOVE_OTHER  = 803,
    CMD_SPLIT_SYNC_OTHER  = 804,
    CMD_PANE0_BACK        = 810,
    CMD_PANE0_FWD         = 811,
    CMD_PANE0_UP          = 812,
    CMD_PANE0_REFRESH     = 813,
    CMD_PANE0_ADDR        = 814,
    CMD_PANE0_COPY_OTHER  = 815,
    CMD_PANE0_MOVE_OTHER  = 816,
    CMD_PANE1_BACK        = 820,
    CMD_PANE1_FWD         = 821,
    CMD_PANE1_UP          = 822,
    CMD_PANE1_REFRESH     = 823,
    CMD_PANE1_ADDR        = 824,
    CMD_PANE1_COPY_OTHER  = 825,
    CMD_PANE1_MOVE_OTHER  = 826,
    CMD_NAV_RECENT_DIRS   = 827,
    CMD_PANE0_RECENT_DIRS = 828,
    CMD_PANE1_RECENT_DIRS = 829,

    // Integrated Per-Image Tools (Resize, Downscale, Target File Size, Flip, Convert)
    CMD_IMG_TOOL_MENU        = 830,
    CMD_IMG_TOGGLE_OVERWRITE = 831,
    CMD_IMG_SCALE_75         = 832,
    CMD_IMG_SCALE_50         = 833,
    CMD_IMG_SCALE_33         = 834,
    CMD_IMG_SCALE_25         = 835,
    CMD_IMG_SCALE_CUSTOM     = 836,
    CMD_IMG_MAXDIM_3840      = 837,
    CMD_IMG_MAXDIM_2560      = 838,
    CMD_IMG_MAXDIM_1920      = 839,
    CMD_IMG_MAXDIM_1280      = 840,
    CMD_IMG_MAXDIM_1024      = 841,
    CMD_IMG_MAXDIM_512       = 842,
    CMD_IMG_TARGET_5MB       = 843,
    CMD_IMG_TARGET_2MB       = 844,
    CMD_IMG_TARGET_1MB       = 845,
    CMD_IMG_TARGET_500KB     = 846,
    CMD_IMG_TARGET_250KB     = 847,
    CMD_IMG_TARGET_100KB     = 848,
    CMD_IMG_TARGET_CUSTOM    = 849,
    CMD_IMG_FLIP_H           = 850,
    CMD_IMG_FLIP_V           = 851,
    CMD_IMG_ROTATE_180       = 852,
    CMD_IMG_CONVERT_JPG_92   = 853,
    CMD_IMG_CONVERT_JPG_80   = 854,
    CMD_IMG_CONVERT_PNG      = 855,
    CMD_IMG_CONVERT_BMP      = 856,
    CMD_CLEAR_RECENT_DIRS    = 857,
    CMD_BASE_RECENT_DIRS     = 900  // 900..949
};

// Layout constants (in DIPs / pixels)
constexpr float kRibbonTabsHeight = 29.0f;
constexpr float kRibbonBodyHeight = 94.0f;
constexpr float kNavBarHeight = 36.0f;
constexpr float kStatusBarHeight = 24.0f;
constexpr float kDetailsHeaderHeight = 25.0f;
constexpr float kDetailsRowHeight = 22.0f;
constexpr float kScrollbarWidth = 14.0f;
constexpr float kFilmstripHeight = 84.0f;
constexpr float kSplitPaneHeaderHeight = 30.0f;

} // namespace Explorer2
