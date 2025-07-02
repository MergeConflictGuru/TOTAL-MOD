// ScriptWindow.cpp
#include "ScriptWindow.h"
#include <wingdi.h> // for layered windows
#include <commctrl.h>
#pragma comment(lib, "Comctl32.lib")

ScriptWindow::ScriptWindow()
    : m_hInst(GetModuleHandleW(nullptr)), m_hWnd(nullptr), m_hEdit(nullptr), m_hButton(nullptr) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
}

ScriptWindow::~ScriptWindow() {
    if (m_hWnd) DestroyWindow(m_hWnd);
    if (m_hEdit) RemoveEscSubclass(m_hEdit);
    if (m_hButton) RemoveEscSubclass(m_hButton);
}

bool ScriptWindow::Open() {
    if (m_initialized) {
        ShowWindow(m_hWnd, SW_RESTORE);
    }
    else if (!InitWindow()) {
        return false;
    }
    SetForegroundWindow(m_hWnd);
    SetFocus(m_hEdit);
    SendMessageW(m_hEdit, EM_SETSEL, 0, -1);
    m_lastOpenTime = GetTickCount();
    return true;
}

bool ScriptWindow::InitWindow() {
    if (!RegisterWindowClass()) return false;

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST;
    m_hWnd = CreateWindowExW(
        exStyle, CLASS_NAME, L"Script Canvas",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 400,
        nullptr, nullptr, m_hInst, this);

    // Fully opaque initially
    SetLayeredWindowAttributes(m_hWnd, 0, 255, LWA_ALPHA);

    ShowWindow(m_hWnd, SW_SHOW);
    m_initialized = true;
    return true;
}

bool ScriptWindow::RegisterWindowClass() {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = m_hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    return RegisterClassExW(&wc) != FALSE;
}

void ScriptWindow::ApplyEscSubclass(HWND child) {
    SetWindowSubclass(child, EscSubclassProc, ESC_SUBCLASS_ID, (DWORD_PTR)this);
}

void ScriptWindow::RemoveEscSubclass(HWND child) {
    RemoveWindowSubclass(child, EscSubclassProc, ESC_SUBCLASS_ID);
}

LRESULT CALLBACK ScriptWindow::EscSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR ref) {
    ScriptWindow* pThis = reinterpret_cast<ScriptWindow*>(ref);
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        pThis->Hide();
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void ScriptWindow::Hide() {
    ShowWindow(m_hWnd, SW_HIDE);
    if (onFocusChange) onFocusChange(false);
}

void ScriptWindow::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT ScriptWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Edit control
        m_hEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            10, 10, 560, 300, m_hWnd, (HMENU)ID_EDIT, m_hInst, nullptr);
        SetWindowTextW(m_hEdit, L"func void main()\r\n{\r\n\t//put your code here\r\n};");
        // Button
        m_hButton = CreateWindowW(L"BUTTON", L"Execute",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 320, 100, 30, m_hWnd, (HMENU)ID_BUTTON, m_hInst, nullptr);
        // Subclass for Escape
        ApplyEscSubclass(m_hEdit);
        ApplyEscSubclass(m_hButton);
        return 0;
    }
    case WM_ACTIVATE: {
        if (LOWORD(wParam) == WA_INACTIVE) {
            SetLayeredWindowAttributes(m_hWnd, 0, 128, LWA_ALPHA);
            if (onFocusChange) onFocusChange(false);
        }
        else {
            SetLayeredWindowAttributes(m_hWnd, 0, 255, LWA_ALPHA);
            if (onFocusChange) onFocusChange(true);
        }
        return 0;
    }
    case WM_SIZE: {
        if (wParam != SIZE_MINIMIZED) {
            RECT rc;
            GetClientRect(m_hWnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            MoveWindow(m_hEdit, 10, 10, w - 20, h - 60, TRUE);
            MoveWindow(m_hButton, 10, h - 40, 100, 30, TRUE);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BUTTON) {
            int len = GetWindowTextLengthW(m_hEdit);
            std::wstring text(len, L'\0');
            GetWindowTextW(m_hEdit, &text[0], len + 1);
            if (onExecute) onExecute(text);
        }
        return 0;
    case WM_CLOSE:
        Hide();
        return 0;
    case WM_DESTROY:
        RemoveEscSubclass(m_hEdit);
        RemoveEscSubclass(m_hButton);
        return 0;
    }
    return DefWindowProcW(m_hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK ScriptWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ScriptWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pThis = (ScriptWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hWnd = hwnd;
    }
    else {
        pThis = (ScriptWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        // Check if we should try to bring to front
        if (pThis->m_lastOpenTime && (GetTickCount() - pThis->m_lastOpenTime) < 550) {
            if (GetForegroundWindow() != pThis->m_hWnd) {
                SetForegroundWindow(pThis->m_hWnd);
                SetFocus(pThis->m_hEdit);
            }
        }

        return pThis->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}