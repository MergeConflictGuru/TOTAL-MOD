#ifndef __STATSWINDOW_H__
#define __STATSWINDOW_H__

#define NOMINMAX
#include <windows.h>
#include <vector>
#include <string>
#include <functional>

class StatsWindow {
public:
    StatsWindow();
    ~StatsWindow();

    bool Open();
    void Run();
    void SetWindowTitle(const std::wstring& title);
    void TriggerRefresh();
    void UpdateStats(const std::vector<std::wstring>& columns,
        std::vector<std::vector<std::wstring>>&& rows);
    inline bool IsReady() { return m_initialized;  }

    std::function<bool(int row, int col, const std::wstring& value)> onCellEdit;

private:
    HWND m_hMainWindow = nullptr;
    HWND m_hListView = nullptr;
    HWND m_hEdit = nullptr;
    HINSTANCE m_hInst = nullptr;
    int m_editingRow = -1;
    int m_editingCol = -1;
    std::wstring m_originalText;
    bool m_initialized = false;

    static constexpr wchar_t CLASS_NAME[] = L"StatsWindowClass";
    static constexpr int ID_LISTVIEW = 100;
    static constexpr int ID_EDIT = 101;

    bool InitWindow();
    void ResizeControls();
    void SetupColumns();
    void PopulateData();
    void StartEditing(int row, int col);
    void FinishEditing(bool commit);
    void AdjustEditSize();
    void PositionEditControl(int row, int col);

    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef);

    std::vector<std::wstring> m_columns;
    std::vector<std::vector<std::wstring>> m_rows;
};

#endif // __STATSWINDOW_H__