#ifndef __ITEMTOOL_H__
#define __ITEMTOOL_H__

#include "ItemWindow.h"
#include <array>
#include <vector>
#include <string>
#include <algorithm>   // std::search, std::max, std::unique
#include <cwctype>     // std::towlower
#include <cwchar>      // std::wcslen
#include <cstddef>     // size_t

int localeNaturalCompare(const wchar_t* a, const wchar_t* b, const wchar_t* locale = L"cs-CZ");
std::wstring cp_to_wstring(const char* input, unsigned cp = 1250);

template <size_t N = 3>
class FlatItemStore {
public:
    // Construct with a set of N headers (each header is a column name)
    FlatItemStore(const std::array<std::wstring, N>& hdrs)
        : m_headers(hdrs) {}

    struct Entry {
        size_t offsets[N];
    };

    // Clears all stored items
    void clear() {
        flatSearch.clear();
        flat.clear();
        index.clear();
    }

    size_t rows() const {
        return index.size();
    }

    static constexpr size_t cols = N;

    // Add N strings in one shot
    void add(const std::array<std::wstring, N>& strs) {
        Entry e;
        // for each of the N strings:
        for (size_t i = 0; i < N; ++i) {
            // record where this string starts
            e.offsets[i] = flat.size();
            // copy raw + null‐term
            flat.insert(flat.end(), strs[i].begin(), strs[i].end());
            flat.push_back(L'\0');
        }
        index.push_back(e);

        // also append lowercase versions for search
        for (size_t i = 0; i < N; ++i) {
            auto lower = toLower(strs[i]);
            flatSearch.insert(flatSearch.end(), lower.begin(), lower.end());
            flatSearch.push_back(L'\0');
        }
    }

    // Substring search (case‐insensitive)
    std::vector<const Entry*> search(const std::wstring& substr) const {
        auto lowerSubstr = toLower(substr);
        std::vector<const Entry*> results;
        auto it = flatSearch.begin();
        while (it != flatSearch.end()) {
            it = std::search(it, flatSearch.end(), lowerSubstr.begin(), lowerSubstr.end());
            if (it == flatSearch.end()) break;

            size_t pos = it - flatSearch.begin();
            if (auto rec = entryForOffset(pos))
                results.push_back(rec);

            ++it; // move past this match
        }
        // dedupe
        results.erase(std::unique(results.begin(), results.end()), results.end());
        return results;
    }

    // View any of the N strings by index
    const wchar_t* view(size_t i, const Entry& e) const {
        return &flat[e.offsets[i]];
    }

    const std::wstring& header(size_t i) const {
        return m_headers[i];
    }

    template <size_t... Cols>
    void sortRows(std::vector<const Entry*>& rows, bool ascending = true) const {
        // Pack the Cols... into a fixed‐size array at compile time.
        static constexpr size_t numCols = sizeof...(Cols);
        static constexpr size_t cols[numCols] = { Cols... };

        std::sort(rows.begin(), rows.end(),
            [this, ascending](const Entry* a, const Entry* b) {
                // Loop over each column index until we find a difference.
                for (size_t i = 0; i < numCols; ++i) {
                    size_t col = cols[i];
                    // get the two strings for column 'col'
                    const wchar_t* wa = view(col, *a);
                    const wchar_t* wb = view(col, *b);

                    int cmp = localeNaturalCompare(wa, wb);
                    if (cmp != 0) {
                        return ascending ? (cmp < 0) : (cmp > 0);
                    }
                    // else they're equal in this column → try next column
                }
                // All specified columns were equal → preserve order (i.e. "false")
                return false;
            }
        );
    }

    std::wstring formatRows(const std::vector<const Entry*>& rows) const {
        // 1) Compute column widths (in wchar_t) based on headers and cell contents
        std::array<size_t, N> widths{};
        for (size_t i = 0; i < N; ++i) {
            widths[i] = m_headers[i].length();
        }

        for (auto e : rows) {
            for (size_t i = 0; i < N; ++i) {
                const wchar_t* wv = view(i, *e);
                widths[i] = std::max(widths[i], std::wcslen(wv));
            }
        }

        std::wstring buf;

        // Helper to append one row (array of N wchar_t*) into buf
        auto appendRow = [&](const std::array<const wchar_t*, N>& cells) {
            for (size_t i = 0; i < N; ++i) {
                const wchar_t* cell = cells[i];
                size_t len = std::wcslen(cell);
                buf.append(cell, len);
                buf.append(widths[i] - len, L' ');
                if (i + 1 < N) buf += L" | ";
            }
            buf += L'\n';
            };

        // 2) Build header line
        {
            std::array<const wchar_t*, N> headerVs{};
            for (size_t i = 0; i < N; ++i) {
                headerVs[i] = m_headers[i].c_str();
            }
            appendRow(headerVs);
        }

        // 3) Separator line
        for (size_t i = 0; i < N; ++i) {
            buf.append(widths[i], L'-');
            if (i + 1 < N) buf += L"-|-";
        }
        buf += L'\n';

        // 4) Each data row
        for (auto e : rows) {
            std::array<const wchar_t*, N> rowVs{};
            for (size_t i = 0; i < N; ++i) {
                rowVs[i] = view(i, *e);
            }
            appendRow(rowVs);
        }

        return buf;
    }

private:
    // Given a flatSearch‐buffer offset, find the corresponding Entry.
    // We compare against offsets[0] (the start of each entry block).
    const Entry* entryForOffset(size_t pos) const {
        auto cmp = [](size_t val, const Entry& e) { return val < e.offsets[0]; };
        auto it = std::upper_bound(index.begin(), index.end(), pos, cmp);
        if (it == index.begin()) return nullptr;
        return &*(--it);
    }

    static std::wstring toLower(const std::wstring& input) {
        std::wstring result;
        result.reserve(input.size());
        for (wchar_t c : input) {
            result.push_back(std::towlower(c));
        }
        return result;
    }

    // Storage for headers
    std::array<std::wstring, N> m_headers{};

    std::vector<wchar_t> flat;
    std::vector<wchar_t> flatSearch;
    std::vector<Entry>   index;
};

#endif