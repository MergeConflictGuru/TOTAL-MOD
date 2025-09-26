#ifndef __ITEMWINDOW_H__
#define __ITEMWINDOW_H__

#define NOMINMAX
#include <windows.h>
#include <vector>
#include <string>
#include <functional>

struct Marker {
    float u;
    float v;

    union {
        uint32_t value; // Full ARGB as 0xAARRGGBB
        struct {
            uint8_t b : 8;
            uint8_t g : 8;
            uint8_t r : 8;
            uint8_t a : 8;
        } argb;
    } color;

    Marker(float u, float v, uint32_t argb)
        : u(u), v(v) {
        color.value = argb;
    }
};
class ItemWindow {
public:
    std::function<void(const std::wstring&)> onSearchChange;
    std::function<void(int /*row*/, int /*col*/)> onRowActivate;  // Renamed from onRowClick
    std::function<void(int /*row*/, int /*col*/, POINT /*screenPt*/)> onRowRightClick;
    std::function<void(int /*row*/)> onSelectionChange;  // New for selection changes
    std::function<void(float /*u*/, float /*v*/, bool /*rightClick*/)> onImageDoubleClick;
    std::function<void()> anyEvt;
    std::function<void(bool)> onFocusChange;

    ItemWindow();
    ~ItemWindow();

    bool Open();
    void Run();
    void Hide();
    void UpdateResults(std::vector<std::wstring>&& columns,
        std::vector<std::vector<std::wstring>>&& rows);
    inline bool IsReady() { return m_initialized; }
    void        ShowImage(const unsigned char* raw_bgra, int w, int h);
    void ShowImage(std::vector<uint8_t>&& raw_bgra, int w, int h);
    void UpdateMarkers(std::vector<Marker>&&);
    void SetWindowTitle(const std::wstring& title);
    void TriggerRefresh();
    inline bool IsShown() { return IsWindowVisible(m_hMainWindow) && !IsIconic(m_hMainWindow) ; }

private:
    HWND        m_hMainWindow = nullptr;
    HWND        m_hSearchEdit = nullptr;
    HWND        m_hResultListView = nullptr;
    std::vector<unsigned char> m_imageData;
    int         m_imgWidth = 0;
    int         m_imgHeight = 0;
    bool        m_initialized = false;
    RECT        m_imgRect = { 0 }; // Stores image display area
    // Store last shown columns/rows for resize handling
    std::vector<std::wstring> m_lastColumns;
    std::vector<std::vector<std::wstring>> m_lastRows;
    HBITMAP m_hBitmap = nullptr; // For transparent image rendering
    DWORD m_lastOpenTime = 0;
    std::vector<Marker> m_markers;

    POINT UVToScreen(float u, float v);
    std::pair<float, float> ScreenToUV(int x, int y);

    bool        InitWindow();
    void        OpenImageDialogAndLoad();
    
    bool        DecodeBMP(const char* fname, std::vector<unsigned char>& out, int& w, int& h);
    bool        DecodeTGA(const char* fname, std::vector<unsigned char>& out, int& w, int& h);
    void        AdjustColumnWidths(); // New helper for column resizing
    void Resize(HWND hwnd);

    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT CALLBACK        WndProc(HWND, UINT, WPARAM, LPARAM);

    void        SetupListViewColumns(const std::vector<std::wstring>& columns);
    void        ClearListView();
    void        PopulateListViewRows(const std::vector<std::vector<std::wstring>>& rows);

    HINSTANCE   m_hInst = nullptr;
    const wchar_t* m_szClassName = L"ItemSearchClass";

    // The actual subclass callback. Must be static so it matches SUBCLASSPROC.
    static LRESULT CALLBACK EscSubclassProc(
        HWND hWndChild,
        UINT uMsg,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR uIdSubclass,
        DWORD_PTR dwRefData
    );

    static constexpr UINT_PTR ESC_SUBCLASS_ID = 0xC0DE;
    void ApplyEscSubclass(HWND hChild);
    void RemoveEscSubclass(HWND hChild);
};

#endif