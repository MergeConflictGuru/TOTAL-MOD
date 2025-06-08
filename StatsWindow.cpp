#include "StatsWindow.h"
#include <Windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <functional>

#pragma comment(lib, "Comctl32.lib")

StatsWindow::StatsWindow() : m_hInst(GetModuleHandleW(nullptr)) {}

StatsWindow::~StatsWindow() {
    if (m_hEdit) {
        RemoveWindowSubclass(m_hEdit, EditProc, 0);
    }
}

bool StatsWindow::Open() {
    if (m_initialized) {
        ShowWindow(m_hMainWindow, SW_RESTORE);
    }
    else if (InitWindow()) {
        ShowWindow(m_hMainWindow, SW_SHOW);
    }
    else return false;

    SetForegroundWindow(m_hMainWindow);

    return true;
}


bool StatsWindow::InitWindow() {
    // Initialize common controls with Unicode support
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    // Explicit Unicode window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    // Create main window with Unicode API
    m_hMainWindow = CreateWindowExW(0, CLASS_NAME, L"Stats Editor",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, nullptr, nullptr, m_hInst, this);

    if (!m_hMainWindow) return false;

    SetWindowPos(
        m_hMainWindow,
        HWND_TOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE // Don’t mess with position or size
    );

    // Create listview with Unicode
    m_hListView = CreateWindowW(WC_LISTVIEWW, L"",
        WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_EDITLABELS,
        0, 0, 0, 0, m_hMainWindow, (HMENU)ID_LISTVIEW, m_hInst, nullptr);

    // Enable full row select and grid lines
    ListView_SetExtendedListViewStyle(m_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // Create edit control with Unicode
    m_hEdit = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_AUTOVSCROLL,
        0, 0, 0, 0, m_hListView, (HMENU)ID_EDIT, m_hInst, nullptr);
    SetWindowSubclass(m_hEdit, EditProc, 0, (DWORD_PTR)this);
    ShowWindow(m_hEdit, SW_HIDE);
    m_initialized = true;
    return true;
}

void StatsWindow::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void StatsWindow::SetWindowTitle(const std::wstring& title) {
    SetWindowTextW(m_hMainWindow, title.c_str());
}

void StatsWindow::TriggerRefresh() {
    InvalidateRect(m_hListView, nullptr, TRUE);
}

void StatsWindow::UpdateStats(const std::vector<std::wstring>& columns,
    std::vector<std::vector<std::wstring>>&& rows) {
    m_columns = std::move(columns);
    m_rows = std::move(rows);

    if (m_hListView) {
        // Clear existing data
        ListView_DeleteAllItems(m_hListView);

        // Delete existing columns
        int colCount = Header_GetItemCount(ListView_GetHeader(m_hListView));
        for (int i = colCount - 1; i >= 0; i--) {
            ListView_DeleteColumn(m_hListView, i);
        }

        SetupColumns();
        PopulateData();
    }
}

void StatsWindow::SetupColumns() {
    RECT rc;
    GetClientRect(m_hListView, &rc);
    int width = (rc.right - rc.left) / std::max(1, (int)m_columns.size());

    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    for (size_t i = 0; i < m_columns.size(); i++) {
        lvc.pszText = const_cast<LPWSTR>(m_columns[i].c_str());
        lvc.cx = width;
        // Explicit Unicode column insertion
        SendMessageW(m_hListView, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&lvc);
    }
}

void StatsWindow::PopulateData() {
    for (size_t row = 0; row < m_rows.size(); row++) {
        // Insert main item with Unicode
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = static_cast<int>(row);
        lvi.iSubItem = 0;
        lvi.pszText = const_cast<LPWSTR>(m_rows[row][0].c_str());
        SendMessageW(m_hListView, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        // Set subitems with Unicode
        for (size_t col = 1; col < m_rows[row].size(); col++) {
            LVITEMW lviSub = {};
            lviSub.iSubItem = static_cast<int>(col);
            lviSub.pszText = const_cast<LPWSTR>(m_rows[row][col].c_str());
            // Using LVM_SETITEMTEXTW for explicit Unicode
            SendMessageW(m_hListView, LVM_SETITEMTEXTW,
                (WPARAM)row,
                (LPARAM)&lviSub);
        }
    }
}

void StatsWindow::PositionEditControl(int row, int col) {
    // Get cell rectangle - use LVIR_BOUNDS for entire cell
    RECT rc = { 0 };
    rc.top = col;
    rc.left = LVIR_BOUNDS;
    if (SendMessageW(m_hListView, LVM_GETSUBITEMRECT, row, (LPARAM)&rc) == FALSE) {
        return;
    }

    // Adjust for header if it's the first row
    if (row == 0) {
        RECT headerRect;
        if (HWND hHeader = ListView_GetHeader(m_hListView)) {
            GetWindowRect(hHeader, &headerRect);
            MapWindowPoints(HWND_DESKTOP, m_hListView, (LPPOINT)&headerRect, 2);
            rc.top += (headerRect.bottom - headerRect.top);
        }
    }

    // Position edit control
    SetWindowPos(m_hEdit, nullptr,
        rc.left, rc.top,
        rc.right - rc.left, rc.bottom - rc.top,
        SWP_NOZORDER | SWP_SHOWWINDOW);
}

void StatsWindow::StartEditing(int row, int col) {
    // Get current text
    wchar_t buffer[256] = { 0 };
    LVITEMW lvi = {};
    lvi.iItem = row;
    lvi.iSubItem = col;
    lvi.pszText = buffer;
    lvi.cchTextMax = 255;
    SendMessageW(m_hListView, LVM_GETITEMTEXTW, row, (LPARAM)&lvi);
    m_originalText = buffer;

    // Position edit control
    PositionEditControl(row, col);
    SetWindowTextW(m_hEdit, buffer);
    SendMessageW(m_hEdit, EM_SETSEL, 0, -1);
    SetFocus(m_hEdit);

    // Store editing position
    m_editingRow = row;
    m_editingCol = col;
}

void StatsWindow::FinishEditing(bool commit) {
    if (m_editingRow < 0 || m_editingCol < 0) return;

    wchar_t buffer[256] = { 0 };
    GetWindowTextW(m_hEdit, buffer, 255);
    std::wstring newText = buffer;

    if (commit) {
        bool acceptEdit = true;
        if (onCellEdit) {
            acceptEdit = onCellEdit(m_editingRow, m_editingCol, newText);
        }

        if (acceptEdit) {
            // Update listview
            LVITEMW lvi = {};
            lvi.iSubItem = m_editingCol;
            lvi.pszText = const_cast<LPWSTR>(newText.c_str());
            SendMessageW(m_hListView, LVM_SETITEMTEXTW, m_editingRow, (LPARAM)&lvi);

            // Update internal data
            if (m_editingRow < static_cast<int>(m_rows.size()) &&
                m_editingCol < static_cast<int>(m_rows[m_editingRow].size())) {
                m_rows[m_editingRow][m_editingCol] = newText;
            }
        }
        else {
            // Revert to original text in listview
            LVITEMW lvi = {};
            lvi.iSubItem = m_editingCol;
            lvi.pszText = const_cast<LPWSTR>(m_originalText.c_str());
            SendMessageW(m_hListView, LVM_SETITEMTEXTW, m_editingRow, (LPARAM)&lvi);
        }
    }
    else {
        // Revert to original text in listview
        LVITEMW lvi = {};
        lvi.iSubItem = m_editingCol;
        lvi.pszText = const_cast<LPWSTR>(m_originalText.c_str());
        SendMessageW(m_hListView, LVM_SETITEMTEXTW, m_editingRow, (LPARAM)&lvi);
    }

    ShowWindow(m_hEdit, SW_HIDE);
    m_editingRow = -1;
    m_editingCol = -1;
    m_originalText.clear();
}

// Main window message handler
LRESULT StatsWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        ResizeControls();
        return 0;

    case WM_NOTIFY: {
        NMHDR* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr->idFrom == ID_LISTVIEW) {
            if (nmhdr->code == NM_DBLCLK) {
                NMITEMACTIVATE* item = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                if (item->iItem >= 0 && item->iSubItem >= 0) {
                    StartEditing(item->iItem, item->iSubItem);
                }
            }
        }
        return 0;
    }

    case WM_CLOSE:
        ShowWindow(m_hMainWindow, SW_HIDE);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hMainWindow, msg, wParam, lParam);
}

// Explicit Unicode window procedure
LRESULT CALLBACK StatsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StatsWindow* pThis = nullptr;

    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<StatsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hMainWindow = hwnd;
    }
    else {
        pThis = reinterpret_cast<StatsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    return pThis ? pThis->HandleMessage(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Edit control subclass procedure
LRESULT CALLBACK StatsWindow::EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uId, DWORD_PTR dwRef) {
    StatsWindow* pThis = reinterpret_cast<StatsWindow*>(dwRef);

    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            pThis->FinishEditing(true);
            return 0;
        }
        else if (wParam == VK_ESCAPE) {
            pThis->FinishEditing(false);
            return 0;
        }
        break;

    case WM_KILLFOCUS:
        pThis->FinishEditing(true);
        break;

    case WM_GETDLGCODE:
        // Handle Enter key properly
        if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN) {
            if (wParam == VK_RETURN) {
                return DLGC_WANTALLKEYS;
            }
        }
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void StatsWindow::ResizeControls() {
    if (m_hListView) {
        RECT rc;
        GetClientRect(m_hMainWindow, &rc);
        SetWindowPos(m_hListView, nullptr, 0, 0, rc.right, rc.bottom, SWP_NOZORDER);
    }
}