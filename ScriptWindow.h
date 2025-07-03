// ScriptWindow.h
#ifndef __SCRIPTWINDOW_H__
#define __SCRIPTWINDOW_H__

#define NOMINMAX
#include <windows.h>
#include <string>
#include <functional>

class ScriptWindow {
public:
    // Callback when Execute button clicked
    std::function<std::wstring(const std::wstring& script)> onExecute;
    // Callback on focus change
    std::function<void(bool focused)> onFocusChange;

    ScriptWindow();
    ~ScriptWindow();

    bool Open();        // Show or create window
    void Run();         // Message loop
    void Hide();        // Hide window

private:
    HINSTANCE m_hInst;
    HWND m_hWnd;
    HWND m_hEdit;
    HWND m_hButton;
    HWND m_hResultText;
    bool m_initialized = false;
    DWORD m_lastOpenTime = 0;

    static constexpr wchar_t CLASS_NAME[] = L"ScriptWindowClass";
    static constexpr int ID_EDIT = 200;
    static constexpr int ID_BUTTON = 201;
    static constexpr UINT_PTR ESC_SUBCLASS_ID = 1;

    bool RegisterWindowClass();
    bool InitWindow();
    void ApplyEscSubclass(HWND child);
    void RemoveEscSubclass(HWND child);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EscSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
        UINT_PTR id, DWORD_PTR ref);
};

#endif // __SCRIPTWINDOW_H__
