#include "ItemWindow.h"
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <vector>
#include <string>
#include <fstream>
#include <windowsx.h>
// Add at top for AlphaBlend
#include <wingdi.h>
#include <cmath> // For std::round

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "uxtheme.lib")

ItemWindow::ItemWindow() {
    m_hInst = GetModuleHandle(nullptr);
    INITCOMMONCONTROLSEX icce = { sizeof(icce), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icce);
}

// In the destructor:
ItemWindow::~ItemWindow() {
    if (m_hMainWindow) {
        DestroyWindow(m_hMainWindow);
    }
    if (m_hBitmap) {
        DeleteObject(m_hBitmap);
    }
}

bool ItemWindow::Open() {
    if (m_initialized) {
        ShowWindow(m_hMainWindow, SW_RESTORE);
    }
    else if (InitWindow()) {
        ShowWindow(m_hMainWindow, SW_SHOW);
    }
    else return false;

    SetForegroundWindow(m_hMainWindow);
    SetFocus(m_hSearchEdit);
    SendMessageW(m_hSearchEdit, EM_SETSEL, 0, -1);

    m_lastOpenTime = GetTickCount();

    return true;
}

// ---------------------------------------------
// 1) The static subclass callback
// ---------------------------------------------
LRESULT CALLBACK ItemWindow::EscSubclassProc(
    HWND hWndChild,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData
) {
    // dwRefData is the `this` pointer we passed in ApplyEscSubclass
    ItemWindow* pThis = reinterpret_cast<ItemWindow*>(dwRefData);

    if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        // Hide the parent window whenever Esc is pressed, even if focus is here
        pThis->Hide();
        return 0; // swallow the key stroke
    }

    // Otherwise, do the default processing for this child control
    return DefSubclassProc(hWndChild, uMsg, wParam, lParam);
}

// ---------------------------------------------
// 2) ApplyEscSubclass: install the subclass on any child HWND
// ---------------------------------------------
void ItemWindow::ApplyEscSubclass(HWND hChild) {
    // If you want to avoid double‐subclassing, you could call RemoveWindowSubclass first.
    // For simplicity, we’ll assume caller knows if it's already applied.

    SetWindowSubclass(
        hChild,
        &ItemWindow::EscSubclassProc,        // pointer to our static callback
        ESC_SUBCLASS_ID,                     // unique ID for this subclass
        reinterpret_cast<DWORD_PTR>(this)    // pass “this” so the callback can call ShowWindow(...)
    );
}

// ---------------------------------------------
// 3) RemoveEscSubclass: uninstall the subclass if needed
// ---------------------------------------------
void ItemWindow::RemoveEscSubclass(HWND hChild) {
    // The lambda overload is optional; passing nullptr also works if you only care about ID
    RemoveWindowSubclass(hChild, &ItemWindow::EscSubclassProc, ESC_SUBCLASS_ID);
}


bool ItemWindow::InitWindow() {
    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = m_hInst;
    wc.lpszClassName = m_szClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        return false;
    }

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST;

    // Increased initial window size
    m_hMainWindow = CreateWindowExW(
        exStyle,
        m_szClassName,
        L"ItemSearch (Search + Results + Image)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, // Larger initial size
        nullptr, nullptr, m_hInst, this
    );
    if (!m_hMainWindow) {
        return false;
    }

    // Right after creation, set it fully opaque (alpha = 255):
    SetLayeredWindowAttributes(m_hMainWindow, 0, 255, LWA_ALPHA);

    // Create controls with temporary size (will be resized in WM_SIZE)
    m_hSearchEdit = CreateWindowExW(
        0, WC_EDITW, L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
        0, 0, 100, 28, // Temporary size
        m_hMainWindow, nullptr, m_hInst, nullptr
    );
    SetWindowTheme(m_hSearchEdit, L"Explorer", nullptr);
    ApplyEscSubclass(m_hSearchEdit);

    m_hResultListView = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 100, 100, // Temporary size
        m_hMainWindow, nullptr, m_hInst, nullptr
    );
    SendMessageW(m_hResultListView, LVM_SETEXTENDEDLISTVIEWSTYLE,
        0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
    SetWindowTheme(m_hResultListView, L"Explorer", nullptr);
    ApplyEscSubclass(m_hResultListView);

    // Trigger layout
    Resize(m_hMainWindow);
    UpdateWindow(m_hMainWindow);

    m_initialized = true;
    return true;
}

void ItemWindow::Run() {
    if (!m_initialized) return;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void ItemWindow::UpdateResults(
    std::vector<std::wstring>&& columns,
    std::vector<std::vector<std::wstring>>&& rows
) {
    if (!m_hResultListView) return;

    // Check if the new data has exactly the same columns + row‐count
    bool sameShape = (columns == m_lastColumns) && (rows.size() == m_lastRows.size());
    if (sameShape) {
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].size() != m_lastColumns.size()) {
                sameShape = false;
                break;
            }
        }
    }

    if (sameShape) {
        // Only update m_lastRows and overwrite each cell’s text
        m_lastRows = std::move(rows);

        for (int row = 0; row < (int)m_lastRows.size(); ++row) {
            const auto& rowData = m_lastRows[row];
            for (int col = 0; col < (int)rowData.size(); ++col) {
                LVITEMW lvi = {};
                lvi.mask = LVIF_TEXT;
                lvi.iItem = row;
                lvi.iSubItem = col;
                lvi.pszText = const_cast<LPWSTR>(rowData[col].c_str());
                SendMessageW(m_hResultListView, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&lvi);
            }
        }
    }
    else {
        // Completely rebuild columns + rows
        m_lastColumns = std::move(columns);
        m_lastRows = std::move(rows);
        SetupListViewColumns(m_lastColumns);
        PopulateListViewRows(m_lastRows);
    }
}


void ItemWindow::SetupListViewColumns(const std::vector<std::wstring>& columns) {
    while (SendMessageW(m_hResultListView, LVM_DELETECOLUMN, 0, 0)) {}
    int colCount = (int)columns.size();
    if (colCount == 0) return;

    RECT rc;
    GetClientRect(m_hResultListView, &rc);
    int totalWidth = rc.right - rc.left;
    int colWidth = totalWidth / colCount;

    for (int i = 0; i < colCount; ++i) {
        LVCOLUMNW lvc = {};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.cx = colWidth - 10;
        lvc.pszText = const_cast<LPWSTR>(columns[i].c_str());
        lvc.iSubItem = i;
        SendMessageW(m_hResultListView, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&lvc);
    }
}

void ItemWindow::AdjustColumnWidths() {
    if (m_lastColumns.empty() || !m_hResultListView) return;

    RECT rc;
    GetClientRect(m_hResultListView, &rc);
    int totalWidth = rc.right - rc.left;
    int colCount = (int)m_lastColumns.size();
    int colWidth = totalWidth / colCount;

    for (int i = 0; i < colCount; ++i) {
        LVCOLUMNW lvc = {};
        lvc.mask = LVCF_WIDTH;
        lvc.cx = colWidth - 10;
        SendMessageW(m_hResultListView, LVM_SETCOLUMNW, (WPARAM)i, (LPARAM)&lvc);
    }
}

void ItemWindow::ClearListView() {
    SendMessageW(m_hResultListView, LVM_DELETEALLITEMS, 0, 0);
}

void ItemWindow::UpdateMarkers(std::vector<std::pair<float, float>>&& markers) {
    m_markers = std::move(markers);
    InvalidateRect(m_hMainWindow, &m_imgRect, TRUE);
}

void ItemWindow::SetWindowTitle(const std::wstring& title) {
    if (m_hMainWindow) {
        SetWindowTextW(m_hMainWindow, title.c_str());
    }
}

void ItemWindow::PopulateListViewRows(
    const std::vector<std::vector<std::wstring>>& rows
) {
    ClearListView();
    for (int row = 0; row < (int)rows.size(); ++row) {
        const auto& rowData = rows[row];
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(rowData[0].c_str());
        SendMessageW(m_hResultListView, LVM_INSERTITEMW, 0, (LPARAM)&item);

        for (int col = 1; col < (int)rowData.size(); ++col) {
            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = row;
            lvi.iSubItem = col;
            lvi.pszText = const_cast<LPWSTR>(rowData[col].c_str());
            SendMessageW(m_hResultListView, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&lvi);
        }
    }
}

#define WM_WANTFRESH (WM_USER + 1)

void ItemWindow::TriggerRefresh() {
    if (m_hMainWindow) PostMessageW(m_hMainWindow, WM_WANTFRESH, 0, 0);
}

void ItemWindow::Resize(HWND hwnd) {
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int clientWidth = rcClient.right - rcClient.left;
    int clientHeight = rcClient.bottom - rcClient.top;
    const int margin = 10;
    const int searchHeight = 28;
    const int topSectionHeight = margin + searchHeight + margin;

    // Resize search box (full width)
    SetWindowPos(m_hSearchEdit, nullptr,
        margin, margin,
        clientWidth - 2 * margin, searchHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // Calculate available height for results/image
    int contentHeight = clientHeight - topSectionHeight - margin;
    if (contentHeight < 0) contentHeight = 0;

    // Calculate list view width (left half)
    int listWidth = (clientWidth - 3 * margin) / 2;

    // Position list view (left side)
    SetWindowPos(m_hResultListView, nullptr,
        margin, topSectionHeight,
        listWidth, contentHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // Set image display area (right side)
    m_imgRect.left = 2 * margin + listWidth;
    m_imgRect.top = topSectionHeight;
    m_imgRect.right = m_imgRect.left + listWidth;
    m_imgRect.bottom = m_imgRect.top + contentHeight;

    // Adjust list view columns
    AdjustColumnWidths();

    // Trigger repaint for image area
    InvalidateRect(hwnd, &m_imgRect, TRUE);
}

// Implement coordinate conversion
std::pair<float, float> ItemWindow::ScreenToUV(int x, int y) {
    // Get actual image display area
    int drawWidth = m_imgRect.right - m_imgRect.left;
    int drawHeight = m_imgRect.bottom - m_imgRect.top;

    float imgAspect = (float)m_imgWidth / m_imgHeight;
    float targetAspect = (float)drawWidth / drawHeight;

    int imageWidth = drawWidth;
    int imageHeight = drawHeight;
    int offsetX = 0;
    int offsetY = 0;

    if (imgAspect > targetAspect) {
        imageHeight = (int)(drawWidth / imgAspect);
        offsetY = (drawHeight - imageHeight) / 2;
    }
    else {
        imageWidth = (int)(drawHeight * imgAspect);
        offsetX = (drawWidth - imageWidth) / 2;
    }

    // Calculate UV coordinates
    float u = 0.0f;
    float v = 0.0f;

    if (imageWidth > 0) {
        u = (float)(x - m_imgRect.left - offsetX) / imageWidth;
        if (u < 0.0f) u = 0.0f;
        else if (u > 1.0f) u = 1.0f;
    }

    if (imageHeight > 0) {
        v = (float)(y - m_imgRect.top - offsetY) / imageHeight;
        if (v < 0.0f) v = 0.0f;
        else if (v > 1.0f) v = 1.0f;
    }

    return { u, v };
}

POINT ItemWindow::UVToScreen(float u, float v) {
    POINT pt = { 0, 0 };

    // Get actual image display area (with aspect ratio preserved)
    int drawWidth = m_imgRect.right - m_imgRect.left;
    int drawHeight = m_imgRect.bottom - m_imgRect.top;

    float imgAspect = (float)m_imgWidth / m_imgHeight;
    float targetAspect = (float)drawWidth / drawHeight;

    int imageWidth = drawWidth;
    int imageHeight = drawHeight;
    int offsetX = 0;
    int offsetY = 0;

    if (imgAspect > targetAspect) {
        // Image is wider than target area
        imageHeight = (int)(drawWidth / imgAspect);
        offsetY = (drawHeight - imageHeight) / 2;
    }
    else {
        // Image is taller than target area
        imageWidth = (int)(drawHeight * imgAspect);
        offsetX = (drawWidth - imageWidth) / 2;
    }

    // Convert UV to image coordinates
    pt.x = m_imgRect.left + offsetX + (int)std::round(u * imageWidth);
    pt.y = m_imgRect.top + offsetY + (int)std::round(v * imageHeight);

    return pt;
}

void ItemWindow::Hide() {
    ShowWindow(m_hMainWindow, SW_HIDE);
    if (onFocusChange)
        onFocusChange(false);
}

LRESULT ItemWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    anyEvt();

    switch (msg) {
    case WM_WANTFRESH: {
        return 0;
    }
    case WM_ACTIVATE: {
        // wParam LOWORD tells you how the activation changed:
        //   WA_INACTIVE = window lost focus; any other WA_* = it became active.
        if (LOWORD(wParam) == WA_INACTIVE) {
            // Window just lost focus → make it semi‑transparent
            // (128 is ~50% opacity; you can pick 180–200 for a lighter fade)
            SetLayeredWindowAttributes(hwnd, 0, (BYTE)128, LWA_ALPHA);
            if (onFocusChange)
                onFocusChange(false);
        }
        else {
            // Window got focus → go back to fully opaque
            SetLayeredWindowAttributes(hwnd, 0, (BYTE)255, LWA_ALPHA);
                if (onFocusChange)
            onFocusChange(true);
        }
        return 0;
    }
    case WM_SIZE: {
        if (wParam == SIZE_MINIMIZED) break;
        if (!m_initialized) break;
        Resize(hwnd);
        return 0;
    }

    case WM_COMMAND: {
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            Hide();
            return 0;
        }
        else if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == m_hSearchEdit) {
            int len = GetWindowTextLengthW(m_hSearchEdit);
            std::wstring newText(len, L'\0');
            GetWindowTextW(m_hSearchEdit, &newText[0], len + 1);
            if (onSearchChange) {
                onSearchChange(newText);
            }
        }
        return 0;
    }

    case WM_KEYDOWN: {
        // Pressing Escape should act like clicking the "X"
        if (wParam == VK_ESCAPE) {
            Hide();
            return 0;
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        // Check if click is in image area
        POINT pt = { x, y };
        if (PtInRect(&m_imgRect, pt)) {
            auto uv = ScreenToUV(x, y);
            if (onImageDoubleClick) onImageDoubleClick(uv.first, uv.second);
        }
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->hwndFrom == m_hResultListView) {
            switch (nm->code) {
            case LVN_ITEMCHANGED: {
                // Handle selection changes
                NMLISTVIEW* lv = (NMLISTVIEW*)lParam;
                if ((lv->uChanged & LVIF_STATE) &&
                    (lv->uNewState & LVIS_SELECTED) != (lv->uOldState & LVIS_SELECTED)) {
                    int selectedRow = ListView_GetNextItem(m_hResultListView, -1, LVNI_SELECTED);
                    if (onSelectionChange) onSelectionChange(selectedRow);
                }
                break;
            }
            case LVN_ITEMACTIVATE: {
                // Handle double-click/Enter activation
                NMLISTVIEW* lv = (NMLISTVIEW*)lParam;
                if (onRowActivate) onRowActivate(lv->iItem, lv->iSubItem);
                break;
            }
            case NM_RCLICK: {
                // Handle right-click
                DWORD pos = GetMessagePos();
                POINT screenPoint = { GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };

                // Convert to client coordinates for hit test
                POINT clientPoint = screenPoint;
                ScreenToClient(m_hResultListView, &clientPoint);

                LVHITTESTINFO hitTest = { 0 };
                hitTest.pt = clientPoint;
                ListView_SubItemHitTest(m_hResultListView, &hitTest);

                if (hitTest.iItem >= 0 && onRowRightClick) {
                    onRowRightClick(hitTest.iItem, hitTest.iSubItem, screenPoint);
                }
                break;
            }
            }
        }
        return 0;
    }

    case WM_HOTKEY:
        return 0;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        return 1;
    }

                      // Modified WM_PAINT handler:
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Fill background first
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));

        if (m_hBitmap) {
            HDC hdcMem = CreateCompatibleDC(hdc);
            SelectObject(hdcMem, m_hBitmap);

            // Set up blend function for transparency
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

            // Calculate aspect ratio-preserving size
            int targetWidth = m_imgRect.right - m_imgRect.left;
            int targetHeight = m_imgRect.bottom - m_imgRect.top;

            float imgAspect = (float)m_imgWidth / m_imgHeight;
            float targetAspect = (float)targetWidth / targetHeight;

            int drawWidth = targetWidth;
            int drawHeight = targetHeight;
            int offsetX = 0;
            int offsetY = 0;

            if (imgAspect > targetAspect) {
                // Image is wider than target area
                drawHeight = (int)(targetWidth / imgAspect);
                offsetY = (targetHeight - drawHeight) / 2;
            }
            else {
                // Image is taller than target area
                drawWidth = (int)(targetHeight * imgAspect);
                offsetX = (targetWidth - drawWidth) / 2;
            }

            // Draw with transparency
            AlphaBlend(
                hdc,
                m_imgRect.left + offsetX,
                m_imgRect.top + offsetY,
                drawWidth,
                drawHeight,
                hdcMem,
                0, 0, m_imgWidth, m_imgHeight,
                bf
            );

            // Draw markers on top
            if (!m_markers.empty()) {
                HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

                for (const auto& marker : m_markers) {
                    POINT pt = UVToScreen(marker.first, marker.second);

                    // Draw a cross marker
                    int size = 5;
                    MoveToEx(hdc, pt.x - size, pt.y, nullptr);
                    LineTo(hdc, pt.x + size, pt.y);
                    MoveToEx(hdc, pt.x, pt.y - size, nullptr);
                    LineTo(hdc, pt.x, pt.y + size);
                }

                SelectObject(hdc, hOldPen);
                DeleteObject(hPen);
            }

            DeleteDC(hdcMem);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        Hide();
        return 0;

    case WM_DESTROY:
        RemoveEscSubclass(m_hSearchEdit);
        RemoveEscSubclass(m_hResultListView);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ItemWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ItemWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pThis = (ItemWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hMainWindow = hwnd;
    }
    else {
        pThis = (ItemWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        // Check if we should try to bring to front
        if (pThis->m_lastOpenTime && (GetTickCount() - pThis->m_lastOpenTime) < 550) {
            if (GetForegroundWindow() != pThis->m_hMainWindow) {
                SetForegroundWindow(pThis->m_hMainWindow);
                if (pThis->m_hSearchEdit) SetFocus(pThis->m_hSearchEdit);
            }
        }

        return pThis->WndProc(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Modified ShowImage with transparency support:
void ItemWindow::ShowImage(const unsigned char* raw_bgra, int w, int h) {
    m_imgWidth = w;
    m_imgHeight = h;

    // Delete old bitmap if exists
    if (m_hBitmap) {
        DeleteObject(m_hBitmap);
        m_hBitmap = nullptr;
    }

    // Create DIB section for alpha blending
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // Negative for top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdc = GetDC(nullptr);
    m_hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);

    if (m_hBitmap && bits) {
        // Copy image data with alpha channel
        memcpy(bits, raw_bgra, (size_t)w * h * 4);
    }

    InvalidateRect(m_hMainWindow, nullptr, TRUE);
}

void ItemWindow::ShowImage(std::vector<uint8_t>&& raw_bgra, int w, int h) {
    m_imgWidth = w;
    m_imgHeight = h;
    m_imageData = std::move(raw_bgra);
    InvalidateRect(m_hMainWindow, nullptr, TRUE);
}
