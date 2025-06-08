#include "ItemTool.h"
#define NOMINMAX
#include <windows.h>

//cz specific
std::wstring cp_to_wstring(const char* input, unsigned cp) {
    if (!input || !*input) return {};

    // Convert CPXXXX → UTF-16
    int wlen = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, input, -1, nullptr, 0);
    if (wlen <= 0) return {};

    std::wstring utf16(wlen, 0);
    MultiByteToWideChar(cp, 0, input, -1, &utf16[0], wlen);

    if (cp == 1250) {
        // Apply Czech-specific character remapping
        for (wchar_t& c : utf16) switch (c) {
        case 0x00E2: c = 0x0159; break;  // â → ř
        case 0x0119: c = 0x011B; break;  // ę → ě
        case 0x0103: c = 0x0165; break;  // ă → ť
        case 0x00C2: c = 0x0158; break;  // Â → Ř
        case 0x00EB: c = 0x010F; break;  // ë → ď
        case 0x0155: c = 0x0148; break;  // ŕ → ň
        default: break;
        }
    }

    return utf16;
}

//cz specific
int localeNaturalCompare(const wchar_t* a, const wchar_t* b, const wchar_t* locale) {
    if (!a) a = L"";
    if (!b) b = L"";

    int result = CompareStringEx(
        locale,
        NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
        a, -1, // -1 means null-terminated
        b, -1,
        nullptr, nullptr, 0
    );

    switch (result) {
    case CSTR_LESS_THAN:    return -1;
    case CSTR_EQUAL:        return 0;
    case CSTR_GREATER_THAN: return 1;
    default:                return 0;
    }
}
