#ifndef __VISION__
#define __VISION__

// This file added in headers queue
// File: "Sources.h"
#include "resource.h"
#include <fcntl.h>
#include <utility>
#include <vector>
#include <cassert>
#include <cwchar> // deprecated but still usable for now
#include <algorithm>
#include <unordered_map>
#include <regex>
#include "ItemTool.h"
#include "PickPocket.h"
#include "ScriptWindow.h"
#include "StatsWindow.h"

namespace std {
    template <>
    struct hash<GOTHIC_ENGINE::zSTRING> {
        size_t operator()(const GOTHIC_ENGINE::zSTRING& s) const {
            uint32_t h = 0x811c9dc5u; // FNV-1a 32-bit offset basis
            size_t l = s.Length();
            auto ch = s.ToChar();
            for (size_t i = 0; i < l; ++i) {
                h ^= static_cast<uint8_t>(ch[i]);
                h *= 0x01000193u; // FNV-1a 32-bit prime
            }
            return static_cast<size_t>(h); // clean cast, same width on 32-bit
        }
    };
}

namespace GOTHIC_ENGINE {
#include "Rake.h"

    void CreateConsole()
    {
        static bool once = true;
        if (!once) return;
        once = false;

        AllocConsole();

        FILE* fpOut, * fpIn, * fpErr;
        freopen_s(&fpOut, "CONOUT$", "w", stdout);
        freopen_s(&fpIn, "CONIN$", "r", stdin);
        freopen_s(&fpErr, "CONOUT$", "w", stderr);

        //_setmode(_fileno(fpOut), _O_U16TEXT);
        _setmode(_fileno(fpIn), _O_U16TEXT);
        //_setmode(_fileno(fpErr), _O_U16TEXT);

        SetConsoleOutputCP(CP_UTF8);     // enable UTF-8 output
        //SetConsoleCP(CP_UTF8);           // enable UTF-8 input (if needed)

        std::cout.clear();
        std::cerr.clear();
        //std::cin.clear();
        //std::wcout.clear();
        //std::wcerr.clear();
        std::wcin.clear();
    }

    #define HOOK_RAW_ORIG(x) (x).GetHookInfo().lpPointer

    static inline std::string wchar_to_str(const wchar_t* wchars) {
        std::string codeStr;
        while (wchars && *wchars)
            codeStr.push_back(static_cast<char>(*(wchars++)));
        return codeStr;
    }

    /*
        QUICKER LOOT
    */

    HOOK Orig_oCNpcDoTakeVob PATCH(&oCNpc::DoTakeVob, &oCNpc::DoTakeVob_Hook);


#define PATS \
    X("ITPL_\0") X("ITMI_GOLD") X("ITLS_TORCH") X("ITRW_ARROW") X("ITRW_BOLT")
#define X(s) s,
    constexpr const char* patterns[] = { PATS };
#undef X
#define X(s) (sizeof(s) - 1),
    constexpr size_t pattern_lens[] = { PATS };
#undef X

    enum NPC_MOVE_RESULT {
        MOVE_SUCCESS = 0,  // Path is clear
        MOVE_STEP_DOWN = 1,  // Shallow drop (safe to step down)
        MOVE_OBSTACLE = 2,  // Blocked by wall/object
        MOVE_JUMP_LEDGE = 3,  // Ledge requires jump
        MOVE_CHASM = 4,  // Deep chasm (fall hazard)
        MOVE_JUMP_CHASM = 9   // Chasm can be jumped over
    };

    zCArray<oCItem*> scanItems(zCVob* root, float range) {
        zCArray<oCItem*> surrounding;
        if (!root || !root->homeWorld) {
            return surrounding; // no items if no root or world
		}

        auto wp = root->GetPositionWorld();
        zTBBox3D bbox;
        bbox.mins = wp - range;
        bbox.maxs = wp + range;

        root->homeWorld->bspTree.bspRoot->CollectVobsInBBox3D(reinterpret_cast<zCArray<zCVob*>&>(surrounding), bbox);
        for (int i = surrounding.GetNum() - 1; i >= 0; --i) {
            auto otherVob = surrounding[i];
            if (otherVob->_GetClassDef() == oCItem::classDef) // only items
            {
                auto otherWp = otherVob->GetPositionWorld();
                if (otherWp.Distance(wp) <= range) {
                    continue;
                }
            }
            surrounding.RemoveIndex(i);
        }
        return surrounding;
    }

    void filterReachableItems(zCArray<oCItem*>& items, float range, bool debug) {
        auto world = ogame->GetGameWorld();
        zVEC3 playerPos = player->bbox3D.GetCenter();
        playerPos[1] = player->bbox3D.maxs[1];
        const float radius = range / 5;  // Search radius around player
        constexpr int numPoints = 13;      // Number of points to check around player
        const int flags = zTRACERAY_VOB_IGNORE_NO_CD_DYN | zTRACERAY_VOB_IGNORE_CHARACTER | zTRACERAY_POLY_IGNORE_TRANSP;
        static constexpr float TWOPI = 2.0f * 3.141592653589793238462643383279502884f;

        zCArray<zCVob*> ignore;
        ignore.AllocAbs(items.GetNum());
        std::memcpy(ignore.GetArray(), items.GetArray(), items.GetNum() * sizeof(decltype(items[0])));
        ignore.InsertEnd(player);

        auto hasClearLineOfSight = [&](const zVEC3& src, const zVEC3& dst) -> bool {
            auto rv = world->TraceRayFirstHit(src, dst - src, &ignore, flags);
            if (debug) zlineCache->Line3D(src, dst, rv ? zCOLOR(255, 0, 0) : zCOLOR(0, 255, 0), 0);
            return !rv;
            };

        zVEC3 testPoints[numPoints];
		bool hasTestPoints = false;

        for (int i = items.GetNum() - 1; i >= 0; --i) {
            auto* item = items[i];

            // Compute the average bbox radius for the item
            zVEC3 bboxCenter = item->bbox3D.GetCenter();
            zVEC3 r = (item->bbox3D.maxs - item->bbox3D.mins) * 0.5f;
            float avgRadius = (r[0] + r[1] + r[2]) / 3.0f;

            // Direction from X to item
            zVEC3 itemSurfaceFacingPlayer = bboxCenter - (bboxCenter - playerPos).Normalize() * avgRadius;

            bool clearView = false;

            if (hasClearLineOfSight(playerPos, itemSurfaceFacingPlayer)) {
                clearView = true;
            }
            else {
                // Generate points around player in a circle
                if (!hasTestPoints) {
					hasTestPoints = true;
                    auto baseAngle = player->trafoObjToWorld.GetAtVector().GetAngleXZ();
                    for (int i = 0; i < numPoints; i++) {
                        float angle = TWOPI * i / numPoints;
                        float dy = cosf((angle + TWOPI * 0.25f) * 17) * radius / 3;
                        zVEC3 offset(cosf(angle - baseAngle) * radius, dy, sinf(angle - baseAngle) * radius);
                        testPoints[i] = playerPos + offset;
                    }
                }

                // Check visibility from each test point
                for (auto& testPoint : testPoints) {
                    if (!hasClearLineOfSight(playerPos, testPoint)) continue;
                    zVEC3 itemSurfaceFacingPoint = bboxCenter - (bboxCenter - testPoint).Normalize() * avgRadius;
                    if (!hasClearLineOfSight(testPoint, itemSurfaceFacingPoint)) continue;
                    clearView = true;
                }
            }

            if (!clearView) {
                items.RemoveIndex(i);
            }
        }
    }

    bool show_debug_rays = false;

    void showLootRays() {
        static constexpr auto BASE_RANGE = 500.0f;
        bool grab_all = zinput->KeyPressed(KEY_LSHIFT);
        float range = BASE_RANGE;
        if (grab_all) range *= 2;
        auto gimme = scanItems(player, range);
		filterReachableItems(gimme, range, true);
    }

    int oCNpc::DoTakeVob_Hook(zCVob* vob) {
        bool isPlayer = (this == ogame->GetSelfPlayerVob());

        if (!isPlayer) return THISCALL(Orig_oCNpcDoTakeVob)(vob);

        static constexpr auto BASE_RANGE = 500.0f;
        bool grab_all = zinput->KeyPressed(KEY_LSHIFT);
        float range = BASE_RANGE;
        if (grab_all) range *= 2;
        auto gimme = scanItems(vob, range);
        auto took = THISCALL(Orig_oCNpcDoTakeVob)(vob);
		if (!took) return 0; // no item taken, exit early

        auto &vobId = vob->GetObjectName();
        for (int i = gimme.GetNum() - 1; i >= 0; --i) {
			auto otherItem = gimme[i];
            auto &otherId = otherItem->GetObjectName();

            bool grab_this = grab_all;
            if (!grab_all) {
                for (auto i = 0; i < _countof(patterns); ++i) {
                    if (strncmp(vobId.ToChar(), patterns[i], pattern_lens[i]) == 0 && strncmp(otherId.ToChar(), patterns[i], pattern_lens[i]) == 0) {
                        grab_this = true;
                        break; // only one match is needed
                    }
                }
            }
            if (otherItem == vob) grab_this = false;
			if (!grab_this) gimme.RemoveIndex(i);
		}

		filterReachableItems(gimme, range, false);

        for (int i = gimme.GetNum() - 1; i >= 0; --i) {
            auto item = gimme[i];
			DoPutInInventory(item);
		}

        /*
        // Create a copy of items to avoid modifying original
        std::vector<oCItem*> items_to_visit = std::move(gimme);
        zVEC3 current_pos = player->GetPositionWorld();
        std::vector<oCItem*> optimized_path;

        while (!items_to_visit.empty()) {
            // Find closest item to current position
            float min_dist = FLT_MAX;
            oCItem* closest_item = nullptr;
            size_t closest_index = 0;

            for (size_t i = 0; i < items_to_visit.size(); i++) {
                float dist = items_to_visit[i]->GetPositionWorld().Distance(current_pos);
                if (dist < min_dist) {
                    min_dist = dist;
                    closest_item = items_to_visit[i];
                    closest_index = i;
                }
            }

            // Add to optimized path and update current position
            if (closest_item) {
                optimized_path.push_back(closest_item);
                current_pos = closest_item->GetPositionWorld();
                items_to_visit.erase(items_to_visit.begin() + closest_index);
            }
        }

        // Execute movement along optimized path
        for (auto& item : optimized_path) {
            oCMsgMovement* go = new oCMsgMovement(oCMsgMovement::EV_GOTOVOB, item);
            player->GetEM()->OnMessage(go, player);
            oCMsgManipulate* take = new oCMsgManipulate(oCMsgManipulate::EV_TAKEVOB, item);
            player->GetEM()->OnMessage(take, player);
        }
        */
        return took;
    }

    /*
    DRUNKARD
    */

    //std::wstring LogTimerStateToWString(const zCTimer& timer) {

    //    std::wostringstream oss;
    //    oss << std::fixed << std::setprecision(6);
    //    oss << L"zCTimer State:\n";
    //    oss << L"  factorMotion: " << timer.factorMotion << L"\n";
    //    oss << L"  frameTimeFloat: " << timer.frameTimeFloat << L"\n";
    //    oss << L"  totalTimeFloat: " << timer.totalTimeFloat << L"\n";
    //    oss << L"  frameTimeFloatSecs: " << timer.frameTimeFloatSecs << L"\n";
    //    oss << L"  totalTimeFloatSecs: " << timer.totalTimeFloatSecs << L"\n";
    //    oss << L"  lastTimer: " << timer.lastTimer << L"\n";
    //    oss << L"  frameTime: " << timer.frameTime << L"\n";
    //    oss << L"  totalTime: " << timer.totalTime << L"\n";
    //    oss << L"  minFrameTime: " << timer.minFrameTime << L"\n";
    //    oss << L"  forcedMaxFrameTime: " << timer.forcedMaxFrameTime << L"\n";
    //    return oss.str();
    //}

    static int drinkingThis = 0;
    static oCMsgManipulate* queuedDrink = nullptr;
    static float bingeStart = 0.0f;

    void StartDrinking(oCItem* item) {
        if (drinkingThis) return;
        if (item->GetSchemeName().IsEmpty())
            return;
        drinkingThis = item->GetInstance();
        ztimer->factorMotion = 1;
        bingeStart = ztimer->totalTimeFloatSecs;
    }

    void StopDrinking() {
        if (drinkingThis) {
            drinkingThis = 0;
            ztimer->factorMotion = 1;
            bingeStart = 0;
        }
    }

    void drinkIfCan() {
        auto player = ogame->GetSelfPlayerVob();
        if (queuedDrink && (!player || !player->GetEM()->messageList.IsInList(queuedDrink))) queuedDrink = nullptr;

        if (drinkingThis) {
            // According to the fundamental theorem of calculus applied to the exponential 2.25^(τ⁄10):
            //   ∫₀ᵗ 2.25^(τ⁄10) dτ = (10⁄ℓn(2.25))·(2.25^(t⁄10) − 1) ⇒ 2.25^(t⁄10) = 1 + ΔS·(ℓn(2.25)⁄10)
            // where ΔS ≡ ∫₀ᵗ 2.25^(τ⁄10) dτ. By algebraic inversion, ∀ τ ∈ ℝ⁺,
            //   2.25^(τ⁄10) = 1 + ΔS·(ℓn(2.25)⁄10). We precompute (ℓn(2.25)⁄10) ≈ 0.0810930 to excise any runtime ℓn(·) or ℯˣ calls.
            // Finally, supₜ factorMotion ≤ 25 to tame the exponential’s rampant growth.
            float drinkingTime = ztimer->totalTimeFloatSecs - bingeStart; // ΔS = ∫₀ᵗ 2.25^(τ⁄10) dτ

            float rawFactor = 1.0f + drinkingTime * 0.0810930f;
            ztimer->factorMotion = (rawFactor > 25.0f) ? 25.0f : rawFactor;

            if (!queuedDrink) {
                if (!(player && player->inventory2.GetAmount(drinkingThis) >= 1))
                {
                    StopDrinking();
                    return;
                }

                auto& drinkName = parser->GetSymbol(drinkingThis)->name;

                oCMsgMovement* standUp = new oCMsgMovement(oCMsgMovement::EV_STANDUP, 0);
                oCMsgWeapon* removeWeaponMsg = new oCMsgWeapon(oCMsgWeapon::EV_REMOVEWEAPON, 0, 0);
                player->GetEM()->OnMessage(standUp, player);
                player->GetEM()->OnMessage(removeWeaponMsg, player);
                queuedDrink = new oCMsgManipulate(oCMsgManipulate::EV_USEITEMTOSTATE, drinkName, player->interactItemCurrentState + 1);
                player->GetEM()->OnMessage(queuedDrink, player);
            }
        }
    }

    HOOK ORIG_oCNpcInventory_HandleEvent PATCH(&oCNpcInventory::HandleEvent, &oCNpcInventory::HandleEvent_Hook);

    HOOK ORIG_oCNpcInventory_Close PATCH(&oCNpcInventory::Close, &oCNpcInventory::Close_Hook);

    void oCNpcInventory::Close_Hook() {
        StopDrinking();
        THISCALL(ORIG_oCNpcInventory_Close)();
    }

    int oCNpcInventory::HandleEvent_Hook(int key) {
        if (key == KEY_RETURN || key == KEY_LCONTROL || key == MOUSE_BUTTONLEFT) {
            auto player = ogame->GetSelfPlayerVob();
            if (owner == player) {
                if (zinput->KeyPressed(KEY_LSHIFT)) {
                    oCItem* item = GetSelectedItem();
                    StartDrinking(item);
                    return 1;
                }
            }
        }
        else if (key == KEY_ESCAPE || key == KEY_TAB) {
            if (drinkingThis) {
                StopDrinking();
                Close();
                return 1;
            }
        }

        return THISCALL(ORIG_oCNpcInventory_HandleEvent)(key);
    }

    /*
        I AM THE SCRIPTOR!!
        Tread carefully, insect… for with but one malformed token, I can unmake your world.
    */

    #define CRASH_PLS { *(int*)0xDEAD = 0; __assume(0); }

    class yFILE : public zFILE {
    public:
        const std::vector<char>* data;
		const zSTRING& path;
        size_t pos;
        bool opened;

        yFILE(const std::vector<char>* dataPtr, const zSTRING &path)
            : zCtor(zFILE), data(dataPtr), pos(0), opened(false), path(path) {
        }

        virtual ~yFILE() {}

        virtual bool Exists() override {
            return data != nullptr;
        }

        virtual int Open(bool /*readOnly*/) override {
            if (!data) return -1;
            pos = 0;
            opened = true;
            return 0;
        }

        virtual int Reset() override {
            if (!data) return -1;
            pos = 0;
            return 0;
        }

        virtual bool Eof() override {
            return !data || pos >= data->size();
        }

        virtual long Size() override {
            return data ? static_cast<long>(data->size()) : 0;
        }

        virtual long Read(void* buf, long cnt) override {
            if (!opened || !data) return 0;
            long rem = (long)data->size() - (long)pos;
            long toRead = cnt < rem ? cnt : rem;
            if (toRead > 0) {
                memcpy(buf, data->data() + pos, toRead);
                pos += toRead;
            }
            return toRead;
        }

        //virtual int Read(zSTRING& out) override {
        //    if (!opened || !data) return 0;
        //    size_t start = pos;
        //    while (pos < data->size() && (*data)[pos] != '\n' && (*data)[pos] != '\r')
        //        ++pos;
        //    size_t len = pos - start;
        //    char* buf = new char[len + 1];
        //    memcpy(buf, data->data() + start, len);
        //    buf[len] = '\0';
        //    out = zSTRING(buf);
        //    delete[] buf;
        //    if (pos < data->size() && (*data)[pos] == '\r') ++pos;
        //    if (pos < data->size() && (*data)[pos] == '\n') ++pos;
        //    return (int)len;
        //}

        //virtual int ReadChar(char& c) override {
        //    if (!opened || !data || Eof()) return 0;
        //    c = (*data)[pos++];
        //    return 1;
        //}

        virtual int Close() override {
            opened = false;
            return 0;
        }

        virtual zSTRING GetFilename() {
            int pos = path.SearchRev("/", 1);
            zSTRING filename = pos < 0 ? path : path.Copy(pos + 1, path.Length() - pos - 1);
            return filename;
        }

        virtual int Read(zSTRING& out)                              CRASH_PLS;
		virtual int ReadChar(char&)                                 CRASH_PLS;
        virtual void SetMode(long)                                  CRASH_PLS;
        virtual long GetMode()                                      CRASH_PLS;
        virtual void SetPath(zSTRING)                               CRASH_PLS;
        virtual void SetDrive(zSTRING)                              CRASH_PLS;
        virtual void SetDir(zSTRING)                                CRASH_PLS;
        virtual void SetFile(zSTRING)                               CRASH_PLS;
        virtual void SetFilename(zSTRING)                           CRASH_PLS;
        virtual void SetExt(zSTRING)                                CRASH_PLS;
        virtual _iobuf* GetFileHandle()                             CRASH_PLS;
        virtual zSTRING GetFullPath()                               CRASH_PLS;
        virtual zSTRING GetPath()                                   CRASH_PLS;
        virtual zSTRING GetDirectoryPath()                          CRASH_PLS;
        virtual zSTRING GetDrive()                                  CRASH_PLS;
        virtual zSTRING GetDir()                                    CRASH_PLS;
        virtual zSTRING GetFile()                                   CRASH_PLS;
        virtual zSTRING GetExt()                                    CRASH_PLS;
        virtual zSTRING SetCurrentDir()                             CRASH_PLS;
        virtual int ChangeDir(bool)                                 CRASH_PLS;
        virtual int SearchFile(zSTRING const&, zSTRING const&, int) CRASH_PLS;
        virtual bool FindFirst(zSTRING const&)                      CRASH_PLS;
        virtual bool FindNext()                                     CRASH_PLS;
        virtual bool DirCreate()                                    CRASH_PLS;
        virtual bool DirRemove()                                    CRASH_PLS;
        virtual bool DirExists()                                    CRASH_PLS;
        virtual bool FileMove(zSTRING, bool)                        CRASH_PLS;
        virtual bool FileMove(zFILE*)                               CRASH_PLS;
        virtual bool FileCopy(zSTRING, bool)                        CRASH_PLS;
        virtual bool FileCopy(zFILE*)                               CRASH_PLS;
        virtual bool FileDelete()                                   CRASH_PLS;
        virtual bool IsOpened()                                     CRASH_PLS;
        virtual int Create()                                        CRASH_PLS;
        virtual int Create(zSTRING const&)                          CRASH_PLS;
        virtual int Open(zSTRING const&, bool)                      CRASH_PLS;
        virtual bool Exists(zSTRING const&)                         CRASH_PLS;
        virtual void Append()                                       CRASH_PLS;
        virtual long Pos()                                          CRASH_PLS;
        virtual int Seek(long)                                      CRASH_PLS;
        virtual int GetStats(zFILE_STATS&)                          CRASH_PLS;
        virtual int Write(char const*)                              CRASH_PLS;
        virtual int Write(zSTRING const&)                           CRASH_PLS;
        virtual long Write(void const*, long)                       CRASH_PLS;
        virtual int Read(char*)                                     CRASH_PLS;
        virtual zSTRING SeekText(zSTRING const&)                    CRASH_PLS;
        virtual zSTRING ReadBlock(long, long)                       CRASH_PLS;
        virtual int UpdateBlock(zSTRING const&, long, long)         CRASH_PLS;
        virtual long FlushBuffer()                                  CRASH_PLS;
    };

    zFILE* CreateZFile_Hooked(const zSTRING& name);

	HOOK Orig_CreateZFile PATCH(&zCObjectFactory::CreateZFile, &zCObjectFactory::CreateZFile_Hooked);

	std::unordered_map<zSTRING, std::vector<char>> yFiles;

    zFILE* zCObjectFactory::CreateZFile_Hooked(const zSTRING& name) {
        if (name.StartWith("YFS://")) {
            auto it = yFiles.find(name);
            const std::vector<char>* ptr = (it != yFiles.end()) ? &it->second : nullptr;
            return new yFILE(ptr, it->first);
        }

        return THISCALL(Orig_CreateZFile)(name); // fallback to original behaviour
    }

	HOOK Orig_parser_error AS(0x0078E270, &zCParser::Error_Hooked);

	zSTRING parserErrorMsg;

    void zCParser::Error_Hooked(zSTRING& msg, int line) {
        THISCALL(Orig_parser_error)(msg, line);
		parserErrorMsg = msg; // store the error message
	}

	HOOK Orig_parser_insert PATCH(&zCPar_SymbolTable::Insert, &zCPar_SymbolTable::Replace);

    bool replaceSymbols = false;
    int zCPar_SymbolTable::Replace(zCPar_Symbol* symbol) {
		int inserted = THISCALL(Orig_parser_insert)(symbol);
		if (!replaceSymbols) return inserted; // if not replacing, return original result
        if (!inserted) {
            // If the symbol already exists, replace it
            int existing = GetIndex(symbol->name.ToChar());
            if (existing < 0 || table[existing]->type != symbol->type) {
				return 0; // cannot replace, type mismatch or not found
            }
			delete table[existing]; // delete the old symbol
			table[existing] = symbol;
        }
		return 1; // inserted successfully
    }

    bool ExecScriptCode(const std::string&& codeIn, std::string& outResult) {
        static int counter = 0;
        auto funcName = "DYNAMIC_SCRIPT" + std::to_string(counter++);
        auto virtualFile = zSTRING(("YFS://" + funcName + ".d").c_str());

        // 1) Find & rename "main", capturing its return type
        std::string code = codeIn;
        static const std::regex mainRe(
            R"(\b((?:func\s+)(void|int|string))\s+main(?=\s*\())"
        );
        std::smatch m;
        std::string retType;
        bool hasMain = std::regex_search(code, m, mainRe);
        if (hasMain) {
            retType = m[2].str();  // "void", "int" or "string"
            code = std::regex_replace(
                code,
                mainRe,
                m[1].str() + " " + funcName
            );
        }

        parserErrorMsg.Clear(); // clear previous error message
        outResult.clear();

        // 2) Parse into virtual yFile
        yFiles[virtualFile] = std::vector<char>(code.begin(), code.end());
        bool prevParse = parser->enableParsing;
        bool prevStopErr = parser->stop_on_error;
        parser->enableParsing = true;
        parser->stop_on_error = true;
		replaceSymbols = true;
        bool err = parser->MergeFile(zSTRING(virtualFile)) < 0;
        parser->enableParsing = prevParse;
        parser->stop_on_error = prevStopErr;
		replaceSymbols = false;

        if (err) {
            return false;
		}
        if (!hasMain) {
            return true;
        }

        auto sym = parser->GetIndex(funcName.c_str());
        if (!sym) return false;

        int* retPtr = (int*)parser->CallFunc(sym);

        if (retType == "int") {
            outResult = std::to_string(*retPtr);
        }
        else if (retType == "string") {
            zSTRING* s = reinterpret_cast<zSTRING*>(*retPtr);
            outResult = s->ToChar();
        }

        return true;
    }


    class ScriptCanvas {
        ScriptWindow script_win;
    public:
        ScriptCanvas() {
            script_win.onExecute = [](const std::wstring& code) {
				std::string result;
                bool success = ExecScriptCode(wchar_to_str(code.c_str()), result);
                if (!success && !parserErrorMsg.IsEmpty()) {
					result = "Error: " + parserErrorMsg;
				}
                return std::wstring{ result.begin(), result.end() };
			};
		}
        void open() {
            zrenderer->Vid_SetScreenMode(zRND_SCRMODE_WINDOWED);
			script_win.Open();
		}
    };
    

    int ConsoleEvalFunc_Hook(zSTRING const&, zSTRING&);
	HOOK Orig_oCGame_ConsoleEvalFunc PATCH(&oCGame::ConsoleEvalFunc, &ConsoleEvalFunc_Hook);
    int ConsoleEvalFunc_Hook(zSTRING const& cmd, zSTRING& result) {
        if (_strnicmp(cmd.ToChar(), "exec", 4) == 0) {
            const int prefixLen = 4; // e.g. "exec"
            std::string toExec = cmd.Length() <= prefixLen + 2 
                ? "" 
                : std::string(cmd.ToChar() + prefixLen + 1, cmd.Length() - (prefixLen + 1));
            std::string execResult;
            if (ExecScriptCode(std::move(toExec), execResult)) {
                result = ("Executed successfully" +
                    (execResult.empty() ? "." : std::string(" with result: ") + execResult)).c_str();
                return 1;
            }
            result = "Execution failed.";
            if (!parserErrorMsg.IsEmpty()) {
                result += "\nError: " + parserErrorMsg;
            }
            return 0; // failure
        }

        return Orig_oCGame_ConsoleEvalFunc(cmd, result);
	}

    void Game_InitConsole_Hook();
    HOOK Orig_Game_InitConsole AS(0x006D01F0, &Game_InitConsole_Hook);
    void Game_InitConsole_Hook() {
        // Call original init so other commands get registered
        Orig_Game_InitConsole();

        // Command name and description
        zSTRING cmdName("exec");
        zSTRING cmdDesc("Execute script code: exec <code>");

        // Register the command on the console
        zcon->Register(&cmdName, &cmdDesc);
    }
    

    /*
        JOURNEY LOG
    */

    /*

    std::vector<zVEC3> journeyLog;
    zSTRING journeyWorld;

    void markJourney() {
        auto player = ogame->GetSelfPlayerVob();
        auto wld = ogame->GetGameWorld();
        if (!player || !wld)
            return;
        auto worldFile = wld->GetWorldFilename();
        if (journeyWorld != worldFile) {
            journeyLog.clear();
            journeyWorld = worldFile;
        }
        static constexpr size_t YEAR = 365 * 24 * 60;
        if (journeyLog.size() >= YEAR) journeyLog.clear();
        journeyLog.push_back(player->GetPositionWorld());
    }

    auto worldToMap(const zVEC3& wp, oCViewDocumentMap* map) {
        zCViewObject* mapObj = map->ViewArrow.ViewParent;
        auto mapX = mapObj->PixelPosition.X;
        auto mapY = mapObj->PixelPosition.Y;
        auto mapW = mapObj->PixelSize.X;
        auto mapH = mapObj->PixelSize.Y;
        auto worldMinX = map->LevelCoords[0]; // dword214
        auto worldMinZ = map->LevelCoords[1];    // dword214
        auto worldMaxX = map->LevelCoords[2];    // dword218
        auto worldMaxZ = map->LevelCoords[3];    // dword21C
        auto worldW = worldMaxX - worldMinX;
        auto worldH = worldMaxZ - worldMinZ;
        float invW = worldW ? 1.0f / worldW : 0.0f;
        float invH = worldH ? 1.0f / worldH : 0.0f;
        float nx = (wp[0] - worldMinX) * invW;
        float nz = (wp[2] - worldMinZ) * invH;
        return std::make_pair(nx, nz);
    }

    class Journey {
        zCPositionKey* keyArr = nullptr;
        zCArray<zCPositionKey*>  keys;
        zCKBSpline               spline;
        zCOLOR                   color;

    public:
        // Constructor: configure color and thickness
        Journey(oCViewDocumentMap* map,
            zCOLOR drawColor = zCOLOR(255, 0, 0))
            : color(drawColor)
        {
            size_t N = journeyLog.size();
            if (N < 4 || !map) return;

            keys.AllocAbs(N);
            keyArr = new zCPositionKey[N];
            for (size_t i = 0; i < N; ++i) {
                float nx, nz;
                std::tie(nx, nz) = worldToMap(journeyLog[i], map);

                auto& k = keyArr[i];
                k.t = float(i) / float(N - 1);
                k.p = zVEC3(nx, nz, 1.1f);
                k.tension = 0.0f;
                k.continuity = 0.0f;
                k.bias = 0.0f;

                keys.InsertEnd(&k);
            }

            spline.InitVars();
            spline.Initialize(keys, 1);
            //spline.ComputeArcLength();
        }

        // Draw with configured color & thickness
        void draw() {
            spline.Draw(color, 0);
        }

        // Destructor: cleanup
        ~Journey() {
            spline.Terminate();
            delete[] keyArr;
            keys.DeleteList();
        }

        // Disable copy & move
        Journey(const Journey&) = delete;
        Journey& operator=(const Journey&) = delete;
        Journey(Journey&&) = delete;
        Journey& operator=(Journey&&) = delete;
    };

    Journey* journey = nullptr;

    HOOK Orig_Draw PATCH(&zCViewDraw::Draw, &zCViewDraw::Draw_Hook);

    void __fastcall zCViewDraw::Draw_Hook() {
        THISCALL(Orig_Draw)();
        if (_GetClassDef() == oCViewDocumentMap::classDef) {
            if (journey) journey->draw();
        }
    }
    */

    /*
        LOOT MAP
    */

    HOOK Orig_oCViewDocumentMap_UpdatePosition PATCH(&oCViewDocumentMap::UpdatePosition, &oCViewDocumentMap::UpdatePosition_Hook);
    HOOK Orig_oCViewDocumentMap_Dtor PATCH(&oCViewDocumentMap::~oCViewDocumentMap, &oCViewDocumentMap::Dtor_Hook);

    enum class PermanentBuffType : int {
        Nope,
        Potion,
        Herb,
        StoneTablet
    };

    struct ItemId {
        union {
            oCItem* item;
            uint16_t treasure;
            unsigned pockets;
        };
        
        enum class Type : uint8_t {
            Item,
            Treasure,
            Pockets
        } type;

        ItemId() = delete;
        ItemId(oCItem* der) : item(der), type(Type::Item) {}
        ItemId(uint16_t treasure) : treasure(treasure), type(Type::Treasure) {}
        ItemId(unsigned pockets) : pockets(pockets), type(Type::Pockets) {}

		operator uint64_t() const {
			return (static_cast<uint64_t>(type) << 32) | reinterpret_cast<uint64_t>(item);
		}
    };

    template<typename Info>
    struct ItemSpot {
        ItemId id;
		zVEC3 position;
        void* holder;
		Info type;
    };

	//void* (__cdecl* game_operator_new)(size_t size) = (void* (__cdecl*)(size_t))0x00565F20;
    //   void(__cdecl* game_operator_delete)(void* ptr) = (void(__cdecl*)(void*))0x00565F60;

    class SafeViewFX {
    public:
        // Construct + init + open in one go
        SafeViewFX(zCViewObject* parent,
            int unk,
            unsigned long x, unsigned long y,
            float w, float h,
            zSTRING& tex)
        {
            view = static_cast<zCViewFX*>(zCViewFX::_CreateNewInstance());
            assert(view && "Failed to create zCViewFX instance");
            view->Init(parent, unk, x, y, w, h, tex);
            view->Open();
        }

        // Move‑only
        SafeViewFX(SafeViewFX&& o) noexcept
            : view(o.view)
        {
            o.view = nullptr;
        }
        SafeViewFX& operator=(SafeViewFX&& o) noexcept {
            if (this != &o) {
                cleanup();
                view = o.view;
                o.view = nullptr;
            }
            return *this;
        }

        // No copies
        SafeViewFX(const SafeViewFX&) = delete;
        SafeViewFX& operator=(const SafeViewFX&) = delete;

        ~SafeViewFX() {
            cleanup();
        }

        // Expose raw pointer if you need direct access
        zCViewFX* get() const noexcept { return view; }

    private:
        zCViewFX* view = nullptr;

        void cleanup() {
            if (!view) return;
            view->Close();
            if (view->ViewParent)
                view->ViewParent->RemoveChild(view);
            view->Release();
            assert(view->refCtr <= 0 && "zCViewFX not fully released");
            delete view;
            view = nullptr;
        }
    };

    struct MapIcon {
        SafeViewFX view;
        ItemId     id;

        MapIcon() = delete;

        MapIcon(ItemId id, zCViewObject* parent, zSTRING& iconTex)
            : view(parent, true, 0, 0, 1.0f, 1.0f, iconTex),
            id(std::move(id))
        {}

        // Move semantics auto‑generated: SafeViewFX is moveable, ItemId too
        MapIcon(MapIcon&&) noexcept = default;
        MapIcon& operator=(MapIcon&&) noexcept = default;

        // No copies
        MapIcon(const MapIcon&) = delete;
        MapIcon& operator=(const MapIcon&) = delete;
    };


    std::vector<MapIcon> mapIcons;

    static PermanentBuffType GetPermanentBuffType(const char* s) {
        if (strncmp(s, "ITPO_PERM_", 10) == 0)
            return PermanentBuffType::Potion;

        if (strncmp(s, "ITPL_PERM_", 10) == 0 || strncmp(s, "ITPL_DEX_", 9) == 0 || strncmp(s, "ITPL_STRENGTH_", 9) == 0)
            return PermanentBuffType::Herb;

        if (strncmp(s, "ITWR_", 5) == 0 && strstr(s, "STONEPLATE") != nullptr)
            return PermanentBuffType::StoneTablet;

        return PermanentBuffType::Nope;
    }

    static uint32_t GetColorForBuffType(PermanentBuffType type) {
        switch (type) {
        case PermanentBuffType::Potion:      return 0xFFFF6060; // Red-ish
        case PermanentBuffType::Herb:        return 0xFF40A060; // Natural green
        case PermanentBuffType::StoneTablet: return 0xFF6060AA; // Gray Blue
        default:                             return 0xFFFFFFFF; // White
        }
    }

    template <typename T, typename Cb>
    void find_vobs(zCWorld* world, Cb cb) {
        zCArray<zCVob*> vobs;
        world->SearchVobListByBaseClass(T::classDef, vobs, 0);
        for (int i = 0; i < vobs.GetNum(); ++i) {
            cb(*reinterpret_cast<T*>(vobs[i]));
        }
    };

    template<typename Info, typename Cb>
	void find_treasure(std::vector<ItemSpot<Info>> & out, Cb filter) {
        auto &wayNet = *ogame->GetGameWorld()->wayNet;
        auto rakeplace = parser->GetSymbol("RAKEPLACE");
		if (!rakeplace) return;

		for (const auto& spot : RakeTreasureTable) {
            auto found = *(int32_t*)rakeplace->GetDataAdr(spot.index);
            if (found) continue;
            auto* wp = wayNet.GetWaypoint(spot.waypoint);
            if (!wp) continue;
			for (unsigned i = 0; i < spot.item_count; ++i) {
                Info type = filter(spot.items[i]);
                if ((bool)type) {
                    out.push_back({
                        (uint16_t)(spot.index | (i << 10)),
                        wp->GetPositionWorld(),
                        wp,
                        type }
					);
                }
			}
		}   
	}

    template <typename Info, typename Cb>
    static auto RefreshLoot(Cb filter) {
        std::vector<ItemSpot<Info>> result;

        if (auto world = ogame->GetGameWorld()) {

            auto push_item = [&](oCItem* item, zCVob* holder) {
                Info type = filter(item->GetObjectName());
                if ((bool)type) {
                    zVEC3 wp = holder->GetPositionWorld();
                    result.push_back({ item, wp, holder, type });
                }
            };

            auto find_pp_loot = [&](oCNpc* npc) {
                if (npc->aiscriptvars[6]) return;
                auto in = parser->GetSymbol(npc->GetInstance());
                
                for (auto & info: npcPickpocketItems)
                    if (!std::strcmp(info.npc, in->name.ToChar()))
                    {
                        Info type = filter(info.item);
                        if ((bool)type) {
                            result.push_back({
                                (unsigned)(in->GetOffset()),
                                npc->GetPositionWorld(),
                                npc,
                                type 
                            });
                        }
                    }
            };

            find_vobs<oCItem>(world, [&](oCItem& item) {
                push_item(&item, &item);
                });

            find_vobs<oCMobContainer>(world, [&](oCMobContainer& container) {
                auto& items = container.containList;

                for (auto b = items.next; b; b = b->next) {
                    oCItem& item = *b->data;
                    push_item(&item, &container);
                }
                });

            auto player = ogame->GetSelfPlayerVob();

            find_vobs<oCNpc>(world, [&](oCNpc& npc) {
                if (&npc == player) // Only process NPCs that are not the player
                    return;

                auto& items = npc.inventory2.inventory;
                for (auto b = items.next; b; b = b->next) {
                    oCItem& item = *b->data;
                    push_item(&item, &npc);
                }

                find_pp_loot(&npc);
            });

            find_treasure(result, filter);
        };

        return result;
    }

    static auto RefreshSuperLoot() {
        return RefreshLoot<PermanentBuffType>(GetPermanentBuffType);
    }

    void oCViewDocumentMap::UpdatePosition_Hook() {
        THISCALL(Orig_oCViewDocumentMap_UpdatePosition)();

        //if (journey) delete journey;
        //journey = new Journey(this);

        mapIcons.clear();

        zCViewObject* mapObj = ViewArrow.ViewParent;
        auto IconW = ViewArrow.PixelSize.X;
        auto IconH = ViewArrow.PixelSize.Y;
        auto mapX = mapObj->PixelPosition.X;
        auto mapY = mapObj->PixelPosition.Y;
        auto mapW = mapObj->PixelSize.X;
        auto mapH = mapObj->PixelSize.Y;
        auto worldMinX = LevelCoords[0]; // dword214
        auto worldMinZ = LevelCoords[1];    // dword214
        auto worldMaxX = LevelCoords[2];    // dword218
        auto worldMaxZ = LevelCoords[3];    // dword21C
        auto worldW = worldMaxX - worldMinX;
        auto worldH = worldMaxZ - worldMinZ;
        if (worldW == 0.0f || worldH == 0.0f)
            return;
        auto invW = 1.0f / worldW;
        auto invH = 1.0f / worldH;

        auto items = RefreshSuperLoot();

        zSTRING aboveTex("O.TGA");
        zSTRING belowTex("U.TGA");

        zVEC3 wpPlaya = ogame->GetSelfPlayerVob()->GetPositionWorld();
        auto howHigh = wpPlaya[1];

        for (auto& found : items) {
            auto& id = found.id;
			auto& wp = found.position;
			auto type = (PermanentBuffType)found.type;
            uint64_t id_packed = id;

            zSTRING* iconTex = (wp[1] > howHigh) ? &aboveTex : &belowTex;

            mapIcons.emplace_back(id, mapObj, *iconTex);
            MapIcon* icon = &mapIcons.back();

            icon->view.get()->TextureColor = (GetColorForBuffType(type));
            zCPosition size; size.X = IconW; size.Y = IconH;
            icon->view.get()->SetPixelSize(size);

            float nx = (wp[0] - worldMinX) * invW;
            float nz = (wp[2] - worldMinZ) * invH;
            int px = int(mapX + nx * mapW) - IconW / 2;
            int py = int(mapY + nz * mapH) - IconH / 2;

            zCPosition pos; pos.X = px; pos.Y = py;
            icon->view.get()->SetPixelPosition(pos);
            icon->view.get()->SetTexture(*iconTex);
        }
    }

    void oCViewDocumentMap::Dtor_Hook() {
        return THISCALL(Orig_oCViewDocumentMap_Dtor)();
    }

    /* SCRIPT FUNC LOGGING */

  //  HOOK Orig_zCParser_CallFunc_ByName
  //      AS(
  //          0x007929D0,
  //          &zCParser::CallFunc_Hook_ByName
  //      );

  //  HOOK Orig_zCParser_CallFunc_ByIndex
  //      AS(
  //          0x007929F0,
  //          &zCParser::CallFunc_Hook_ByIndex
  //      );

  //  static std::unordered_map<char*, int> funcCallCount;

  //  void log_count(char* tag, char* name) {
  //      int n = ++funcCallCount[name];
  //      int m = n;
  //      // Reduce m by factors of 10 until it’s < 10 or no longer divisible
  //      while (m > 2 && m % 10 == 0) m /= 10;
  //      if (n <= 2 || m == 1) {
  //          std::printf(
  //              "[%s] \"%s\" called %d time%s\n",
  //              tag,
  //              name,
  //              n,
  //              (n == 1 ? "" : "s")
  //          );
  //      }
  //  }

  //  void* zCParser::CallFunc_Hook_ByName(zSTRING const& funcName)
  //  {
		//log_count("zCParser::CallFunc", funcName.ToChar());
  //      return (this->*Orig_zCParser_CallFunc_ByName)(funcName);
  //  }


  //  void* __cdecl CallFunc_Hook_ByIndex_help(int _retaddr, void* _this, int index) {
		//log_count("zCParser::CallFunc", parser->GetSymbol(index)->name.ToChar());
  //      return HOOK_RAW_ORIG(Orig_zCParser_CallFunc_ByIndex);
  //  }

  //  __declspec(naked) void* __cdecl zCParser::CallFunc_Hook_ByIndex(int index, ...)
  //  {
  //      __asm {
  //          call    CallFunc_Hook_ByIndex_help         // call the C++ logger
  //          jmp    eax
  //      }
  //  }

    /*
        ITEM SEARCH
    */

    HOOK Orig_printf AS(0x7D42D7, &printf);

    enum RealIdx {
        REAL_STRENGTH = 81,
        REAL_DEXTERITY = 82,
        REAL_MANA_MAX = 83,
        REAL_TALENT_1H = 84,
        REAL_TALENT_2H = 85,
        REAL_TALENT_BOW = 86,
        REAL_TALENT_XBOW = 87
    };

    // 3) “Current” attribute IDs
    enum CurrAttr {
        ATR_HITPOINTS_MAX = 1,
        ATR_MANA_MAX = 3,
        ATR_STRENGTH = 4,
        ATR_DEXTERITY = 5
    };

    // 4) NPC talent slots
    enum TalentSlot {
        NPC_TALENT_1H = 1,
        NPC_TALENT_2H = 2,
        NPC_TALENT_BOW = 3,
        NPC_TALENT_XBOW = 4
    };

    // 5) Column indices
    enum Col { COL_REAL = 0, COL_CURR = 1, COL_SKILL = 2, COL_COUNT = 3 };

    // 6) Tiny struct to hold get/set for a single column
    struct Stat {
        std::function<int(oCNpc*)>  get;
        std::function<void(oCNpc*, int)> set; // nullptr = read-only
    };

    // 7) Each row has a name + an array of 4 column-wise Stat objects
    struct StatRow {
        std::wstring name;
        std::array<Stat, COL_COUNT> cols;
    };

    static const StatRow rowsSpec[] = {
        // Strength
        {
            L"💪 Strength",
            {{
                    // REAL
                    { [](oCNpc* p) { return p->aiscriptvars[REAL_STRENGTH]; },
                    [](oCNpc* p,int v) { p->aiscriptvars[REAL_STRENGTH] = v;  } },
                    // CURR
                    { [](oCNpc* p) { return p->GetAttribute(ATR_STRENGTH); },
                      [](oCNpc* p, int v) { p->SetAttribute(ATR_STRENGTH, v); } },
                      // SKILL
                      { nullptr, nullptr }
                  }}
        },
        // Dexterity
        {
            L"🏃 Dexterity",
            {{
                { [](oCNpc* p) { return p->aiscriptvars[REAL_DEXTERITY]; },
                [](oCNpc* p,int v) { p->aiscriptvars[REAL_DEXTERITY] = v;  } },
                { [](oCNpc* p) { return p->GetAttribute(ATR_DEXTERITY); },
                  [](oCNpc* p, int v) { p->SetAttribute(ATR_DEXTERITY, v); } },
                { nullptr, nullptr }
            }}
        },
        // Mana Max
        {
            L"🔮 Mana Max",
            {{
                { [](oCNpc* p) { return p->aiscriptvars[REAL_MANA_MAX]; },
                [](oCNpc* p,int v) { p->aiscriptvars[REAL_MANA_MAX] = v;  } },
                { [](oCNpc* p) { return p->GetAttribute(ATR_MANA_MAX); },
                  [](oCNpc* p, int v) { p->SetAttribute(ATR_MANA_MAX, v); } },
                { nullptr, nullptr }
            }}
        },
        // Max HP
        {
            L"❤️ Max HP",
            {{
                { nullptr, nullptr },
                { [](oCNpc* p) { return p->GetAttribute(ATR_HITPOINTS_MAX); },
                  [](oCNpc* p, int v) { p->SetAttribute(ATR_HITPOINTS_MAX, v); } },
                { nullptr, nullptr }
            }}
        },
        // 1H Talent
        {
            L"🗡️ 1H Talent",
            {{
                { [](oCNpc* p) { return p->aiscriptvars[REAL_TALENT_1H]; },
                [](oCNpc* p,int v) { p->aiscriptvars[REAL_TALENT_1H] = v;  } },
                { [](oCNpc* p) { return (p->hitChance[NPC_TALENT_1H]); },
                  [](oCNpc* p,int v) { p->hitChance[NPC_TALENT_1H] = (v); } },
                { [](oCNpc* p) { return p->GetTalentSkill(NPC_TALENT_1H); },
                  [](oCNpc* p,int v) { p->SetTalentSkill(NPC_TALENT_1H, v); } }
            }}
        },
        // 2H Talent
        {
            L"🗡️ 2H Talent",
            {{
                { [](oCNpc* p) { return p->aiscriptvars[REAL_TALENT_2H]; },
                [](oCNpc* p,int v) { p->aiscriptvars[REAL_TALENT_2H] = v;  } },
                { [](oCNpc* p) { return p->hitChance[NPC_TALENT_2H]; },
                  [](oCNpc* p,int v) { p->hitChance[NPC_TALENT_2H] = (v); } },
                { [](oCNpc* p) { return p->GetTalentSkill(NPC_TALENT_2H); },
                  [](oCNpc* p,int v) { p->SetTalentSkill(NPC_TALENT_2H, v); } }

            }}
        },
        // Bow Talent
        {
            L"🏹 Bow Talent",
            {{
                { [](oCNpc* p) { return p->aiscriptvars[REAL_TALENT_BOW]; },
                  [](oCNpc* p,int v) { p->aiscriptvars[REAL_TALENT_BOW] = v;  } },
                { [](oCNpc* p) { return p->hitChance[NPC_TALENT_BOW]; },
                  [](oCNpc* p,int v) { p->hitChance[NPC_TALENT_BOW] = (v); } },
                { [](oCNpc* p) { return p->GetTalentSkill(NPC_TALENT_BOW); },
                  [](oCNpc* p,int v) { p->SetTalentSkill(NPC_TALENT_BOW, v); } }

            }}
        },
        // Xbow Talent
        {
            L"🏹 Xbow Talent",
            {{
                { [](oCNpc* p) { return p->aiscriptvars[REAL_TALENT_XBOW]; },
                [](oCNpc* p,int v) { p->aiscriptvars[REAL_TALENT_XBOW] = v; }},
                { [](oCNpc* p) { return p->hitChance[NPC_TALENT_XBOW]; },
                  [](oCNpc* p,int v) { p->hitChance[NPC_TALENT_XBOW] = (v); } },
                { [](oCNpc* p) { return p->GetTalentSkill(NPC_TALENT_XBOW); },
                  [](oCNpc* p,int v) { p->SetTalentSkill(NPC_TALENT_XBOW, v); } }
            }}
        }
    };

    struct PlayerStatsWindow {
        PlayerStatsWindow() {
            statsWin.onCellEdit = [](int rowIdx, int colIdx, const std::wstring& newVal) {
                // colIdx: 1=Real, 2=Curr, 3=Skill, 4=Hit%
                int arrayIdx = colIdx - 1;
                if (arrayIdx < 0 || arrayIdx >= COL_COUNT) {
                    CMessageW::Error(L"Invalid column.");
                    return 0;
                }

                auto& cellSpec = rowsSpec[rowIdx].cols[arrayIdx];
                if (!cellSpec.set) {
                    CMessageW::Error(L"This field is read-only.");
                    return 0;
                }

                int parsed = 0;
                try { parsed = std::stoi(newVal); }
                catch (...) {
                    CMessageW::Error(L"Invalid number format.");
                    return 0;
                }

                auto playa = ogame->GetSelfPlayerVob();
                if (!playa) {
                    CMessageW::Error(L"No player found.");
                    return 0;
                }

                // Call the setter
                cellSpec.set(playa, parsed);

                return 1;
                };
        }

        void ShowPlayerStatsWindow()
        {
            zrenderer->Vid_SetScreenMode(zRND_SCRMODE_WINDOWED);
            if (!statsWin.Open()) {
                CMessageW::Error(L"Failed to open StatsWindow.");
                return;
            }
            refreshPlayerStats();
        }

        void refreshPlayerStats() {
            if (!statsWin.IsReady()) return;

            // 1) Grab the player Vob
            auto playa = ogame->GetSelfPlayerVob();
            if (!playa) {
                CMessageW::Error(L"No player found.");
                return;
            }

            // 9) Build the column headers
            static const std::vector<std::wstring> columns = { L"Category", L"Real", L"Curr", L"Skill" };

            // 10) Turn our specs into the 2D wstring rows for StatsWindow
            std::vector<std::vector<std::wstring>> data;
            data.reserve(_countof(rowsSpec));
            for (auto& spec : rowsSpec) {
                std::vector<std::wstring> row = { spec.name };
                for (int c = 0; c < COL_COUNT; ++c) {
                    if (spec.cols[c].get) {
                        int val = spec.cols[c].get(playa);
                        row.push_back(std::to_wstring(val));
                    }
                    else {
                        row.push_back(L"-");
                    }
                }
                data.push_back(std::move(row));
            }

            statsWin.SetWindowTitle(L"Player Stats Editor");
            statsWin.UpdateStats(columns, std::move(data));
        }

    private:
        StatsWindow statsWin;
    };

    using Game_CreateInstance_t = void(__cdecl*)(const zSTRING&, zSTRING&);
    auto Game_CreateInstance = (Game_CreateInstance_t)0x006CB7C0;

    class ItemTool {
    public:
        ItemWindow w;
        bool m_imHere = false;
        float wx = 0, wz = 0, ww = 0, wh = 0;
        FlatItemStore<3> insertCodes = FlatItemStore<3>({ L"Code", L"Name" , L"Description" });
        decltype(insertCodes.search(L"")) tableResults;

        struct TableResultInWorld {
            decltype(tableResults)::value_type v;
            int foundCount;
        };

        using NItemSpot = ItemSpot<const zSTRING*>;

        std::unordered_map<zSTRING, TableResultInWorld> resultsByItemName;
        std::unordered_multimap<void*, NItemSpot&> resultsByHolder;
        std::vector<NItemSpot> fond;
        const char *currentMap = nullptr;
        int selectedRow = -1;
        bool needRefresh = false;

        zSTRING namePls(int row) {
            if (row > (int)tableResults.size() || row < 0)
                return "";
            auto res = tableResults[row];
            return wchar_to_str(insertCodes.view(0, *res)).c_str();
        };

        auto markTreasure(zSTRING& findName) {
            for (auto& x : resultsByItemName)
                x.second.foundCount = 0;

            fond = RefreshLoot<const zSTRING*>([&](const zSTRING& itn) -> const zSTRING* {
                if (!findName.IsEmpty() && (strcmp(itn.ToChar(), findName.ToChar()) != 0))
                    return nullptr;
                auto it = resultsByItemName.find(itn);
                if (it != resultsByItemName.end()) {
                    ++it->second.foundCount;
                    return &it->first;
                }
                return nullptr;
                });

            resultsByHolder.clear();
            for (auto& thing : fond) {
                resultsByHolder.emplace(thing.holder, thing);
            }

            std::vector<std::pair<float, float>> markers;
            for (auto& thing : fond) {
                float invW = 1.0f / ww;
                float invH = 1.0f / wh;
                float u = (thing.position[0] - wx) * invW;
                float v = (thing.position[2] - wz) * invH;
                markers.emplace_back(u, v);
            }

            return markers;
        }

        void makeTableAndMap() {

            auto treasure = markTreasure(selectedRow >= 0 ? namePls(selectedRow) : "");

            // 1) Get the headers directly from FlatItemStore<3>:
            std::vector<std::wstring> cols;
            for (size_t i = 0; i < insertCodes.cols; ++i) {
                cols.push_back(insertCodes.header(i));
            }
            cols.push_back(L"Num");

            // 2) Build rows from the matched entries:
            std::vector<std::vector<std::wstring>> rows;
            for (auto entryPtr : tableResults) {
                std::vector<std::wstring> row;
                for (size_t i = 0; i < insertCodes.cols; ++i) {
                    const wchar_t* text = insertCodes.view(i, *entryPtr);
                    row.emplace_back(text);
                }

                auto ite = resultsByItemName.find(wchar_to_str(insertCodes.view(0, *entryPtr)).c_str());
                if (ite != resultsByItemName.end()) row.push_back(L"x" + std::to_wstring(ite->second.foundCount));
                else row.push_back(L"-");
                rows.push_back(std::move(row));
            }

            // 3) Finally, feed the UI with dynamic headers + real data:
            w.UpdateResults(std::move(cols), std::move(rows));

            if (updateMap())
                this->w.UpdateMarkers(std::move(treasure));

            w.SetWindowTitle(L"Found " + std::to_wstring(fond.size()) + L" items!");
        }

        void open() {
            if (!w.IsReady()) {
                w.onSearchChange = [this](const std::wstring& newText) {
                    // If store is empty, load real items:
                    if (insertCodes.rows() == 0) {
                        DiscoverItems();
                    }

                    // Do a case-insensitive substring search:
                    tableResults = insertCodes.search(newText);
                    resultsByItemName.clear();
                    if (!tableResults.empty()) {
                        insertCodes.sortRows<1, 2>(tableResults, true);
                        for (auto entryPtr : tableResults) {
                            const wchar_t* wchars = insertCodes.view(0, *entryPtr);
                            resultsByItemName[wchar_to_str(wchars).c_str()] = { entryPtr, 0 };
                        }
                    }

                    selectedRow = -1;
                    makeTableAndMap();

                    RefreshLater(false);
                    };

                // Example: when a row is clicked, show a popup
                w.onRowActivate = [this](int row, int col) {
                    zSTRING outStatus;
                    auto codeStr2 = namePls(row);
                    Game_CreateInstance(codeStr2, outStatus);
                    };

                w.onSelectionChange = [this](int row) {
                    selectedRow = row;
                    RefreshNow();
                    };

                w.onImageDoubleClick = [this](float u, float v) {
                    auto world = ogame->GetGameWorld();
                    auto worldbb = world->bspTree.bspRoot->bbox3D;
                    zVEC3 wpos;
                    wpos[0] = wx + u * ww;
                    wpos[1] = worldbb.maxs[1];
                    wpos[2] = wz + v * wh;

                    auto& wayNet = *world->wayNet;
                    auto wp = wayNet.GetNearestWaypoint(wpos);
                    auto p = ogame->GetSelfPlayerVob();
                    if (wp && wp->CanBeUsed(p)) {
                        p->BeamTo(wp->GetName());
                        RefreshLater();
                    }
                    };

                w.onRowRightClick = [this](int row, int col, POINT where_) {
                    };

                w.anyEvt = [this]() {
                    if (!needRefresh) return;
                    RefreshNow();
                };

                RefreshLater();
            }

            zrenderer->Vid_SetScreenMode(zRND_SCRMODE_WINDOWED);
            w.Open();
        }

        /* returns worldMatchesMap */
        bool updateMap() {
            static constexpr struct {
                const char* mapTGA;
                const char* worldZen;
                float minX, minZ, maxX, maxZ;
            } maps[] = {
                { "Map_NewWorld_City.tga", "NewWorld\\NewWorld.zen", -6900.0f, 11800.0f, 21600.0f, -9400.0f },
                { "Map_NewWorld.tga", "NewWorld\\NewWorld.zen", -28000.0f, 50500.0f, 95500.0f, -42500.0f },
                { "Map_OldWorld.tga", "OldWorld\\OldWorld.zen", -78500.0f, 47500.0f, 54000.0f, -53000.0f },
                { "Map_AddonWorld_Treasures.tga", "Addon\\AddonWorld.zen", -47783.0f, 36300.0f, 43949.0f, -32300.0f },
            };

            auto wld = ogame->GetGameWorld();
            auto worldFile = wld->GetWorldFilename();

            for (const auto& map : maps) {
                if (_stricmp(worldFile, map.worldZen) == 0) {
                    if (player) {
                        auto wp = player->GetPositionWorld();
                        bool insideMap = (wp[0] >= map.minX && wp[0] <= map.maxX) && (wp[2] >= map.maxZ && wp[2] <= map.minZ);
                        if (!insideMap) continue;
                    }

                    if (currentMap == map.mapTGA) return true;
                    currentMap = map.mapTGA;

                    int w, h;
                    zCTextureFileFormatTGA t;
                    zCTexConGeneric con;
                    t.LoadTexture(zSTRING(map.mapTGA), &con);
                    con.ConvertToNewFormat(zRND_TEX_FORMAT_RGBA_8888);
                    w = con.GetTextureInfo().sizeX;
                    h = con.GetTextureInfo().sizeY;
                    void* data;
                    int stride;
                    con.GetTextureBuffer(0, data, stride);
                    this->w.ShowImage((uint8_t*)data, w, h);
                    t.Release();

                    wx = map.minX;
                    wz = map.minZ;
                    ww = map.maxX - wx;
                    wh = map.maxZ - wz;
                    return true;
                }
            }
            return false;
        }

        void RefreshNow() {
            if (!this->w.IsReady()) return;
            if (!this->w.IsShown()) return;
            needRefresh = false;
            visualize();
        }

        void storeVisible() {
            m_imHere = this->w.IsShown();
        }

        inline bool isVisible() {
            return m_imHere;
        }

        void RefreshLater(bool yes = true) {
            needRefresh = yes;
            if (yes) w.TriggerRefresh();
        }

        void visualize() {
            if (!this->w.IsReady()) return;
            if (!this->w.IsShown()) return;
            makeTableAndMap();
        }

        template<typename Cb>
        static void iterSymbols(Cb cb) {
            auto count = parser->symtab.GetNumInList();
            for (int i = 0; i < count; i++) {
                auto sym = parser->GetSymbolInfo(i);
                if (!sym) continue;
                if (!sym->parent) continue;
                cb(sym);
            }
        }

        void DiscoverItems() {
            zSTRING C_Item("C_Item");
            int idxOfItem = parser->GetIndex(&C_Item);


            oCItem* dummy = static_cast<oCItem*>(oCItem::_CreateNewInstance());
            dummy->Release();
            zSTRING realName;
            std::wstring wReal, wDesc;

            insertCodes.clear();

            auto count = parser->symtab.GetNumInList();
            for (int i = 0; i < count; i++) {
                auto sym = parser->GetSymbolInfo(i);
                if (!sym) continue;
                if (!sym->parent) continue;
                if ((sym->type & 0xF) != 0x7) continue; // ?????

                int baseIdx = parser->GetBase(i);
                auto base = parser->GetSymbolInfo(baseIdx);
                if ((base->type & 0xF) == 0x6) baseIdx = parser->GetBase(baseIdx);

                if (baseIdx == idxOfItem) {
                    auto sym = parser->GetSymbolInfo(i);
                    const char* symName = sym->name.ToChar();

                    dummy->oCItem::oCItem();
                    parser->CreateInstance(i, dummy);
                    realName = dummy->GetName(0);
                    zSTRING& desc = dummy->GetDescription();

                    wReal = !realName.IsEmpty() ? cp_to_wstring(realName.ToChar()) : L"";
                    wDesc = !desc.IsEmpty() ? cp_to_wstring(desc.ToChar()) : L"";

                    dummy->Release();
                    dummy->~oCItem();

                    insertCodes.add({ std::wstring(symName, symName + std::strlen(symName)), wReal, wDesc });
                }
            }
            delete dummy;
        }

    };

    ItemTool item_tool;
    ScriptCanvas script_canvas;
    PlayerStatsWindow player_stats;

    /*
        XRAY
    */
    uint8_t xray_enabled = 0;

    void xr_Detoured();
    void xr_FakeExit1();
    void xr_FakeExit2();

    HOOK xr_real_get_focusHook AS(0x006C35A0, &xr_Detoured);
    HOOK xr_real_exit_1Hook AS(0x006C3844, &xr_FakeExit1);
    HOOK xr_real_exit_2Hook AS(0x006C3BB2, &xr_FakeExit2);

    LPVOID xr_real_get_focus{ HOOK_RAW_ORIG(xr_real_get_focusHook) };
    LPVOID xr_real_exit_1{ HOOK_RAW_ORIG(xr_real_exit_1Hook) };
    LPVOID xr_real_exit_2{ HOOK_RAW_ORIG(xr_real_exit_2Hook) };

    // restore regs after loop
    // esp = unchanged
    // eax, ecx, edx, edi = overwritten
    struct {
        uint32_t ebx, esi, ebp;
    }
    xr_ctx;
    struct {
        void* data = nullptr;
        int numAlloc = -1;
        int num = -1;
    } xr_vobs;

    oCVob* __fastcall xr_filter(oCVob* me) {
        if (!item_tool.isVisible()) return me;
        if (item_tool.resultsByHolder.size() == 0) return me;
        if (item_tool.resultsByHolder.count(me) > 0) return me;
        return nullptr;
    }

    __declspec(naked) void xr_Detoured()
    {
        __asm
        {
            mov al, xray_enabled
            test al, al
            jz USE_ORIGINAL_FOCUS       // first of loop, no need to restore context

            mov eax, ds:0x008D7F94     // zCCamera::activeCam
            test eax, eax               // this totally a bug in the base game but they never found out
            jz USE_ORIGINAL_FOCUS

            mov edx, xr_vobs.num        // edx = vob_count
            test edx, edx
            js INITIALIZE_LOOP

            mov ebx, xr_ctx.ebx               // restore these each subsequent loop iteration
            mov esi, xr_ctx.esi
            mov ebp, xr_ctx.ebp
            mov ecx, xr_vobs.data
            jmp CONTINUE_LOOP

        INITIALIZE_LOOP :                // back that shit up, get vob array
            mov ds : 0x008B21E8, 0        // disable focus bar
            mov xr_ctx.ebx, ebx
            mov xr_ctx.esi, esi
            mov xr_ctx.ebp, ebp
            mov ebx, ecx                // backup ecx = player

            push xr_vobs.data
            mov eax, 0x00565F80         // void __cdecl operator delete[](void *Memory)
            call eax
            add esp, 4
            mov xr_vobs.data, 0
            mov xr_vobs.numAlloc, 0
            mov xr_vobs.num, 0

            push 0x44fa0000             // 2000.0f
            push offset xr_vobs
            mov ecx, ebx                // restore player
            mov eax, 0x0075D730         // void __thiscall oCNpc::CreateVobList(oCNpc* this, zCArray<zCVob*>& xr_vobs, float dist)
            call eax
            mov ecx, xr_vobs.data
            mov edx, xr_vobs.num

        CONTINUE_LOOP :
            dec edx
            js LAST_ITER
            mov ebx, [ecx + edx * 4]    // vob ptr
            mov eax, [ebx]
            call[eax]                  // zCClassDef *__thiscall zCObject::_GetClassDef(zCObject* this), (ecx ignored)

            cmp eax, 0x00AB1168         // oCItem
            jne FILTER_NOT_ITEM
            mov eax, [ebx + 0x158]      // ~ oCItem::HasFlag(vob, 0x800000) ...igs not being used by npc
            test eax, 0x800000
            jz VISIBLE

        FILTER_NOT_ITEM :
            cmp eax, 0x00AB18B0         // oCMobContainer
            jne FILTER_NOT_CONTAINER
            mov eax, [ebx + 0x280]      // ~ oCMobContainer.itemList.first ...is not empty
            test eax, eax
            jnz VISIBLE

        FILTER_NOT_CONTAINER :
            cmp eax, 0x00AB1E20         // oCNpc
            jne CONTINUE_LOOP
            mov eax, [ebx + 0x1B8]      // ~ bool __thiscall oCNpc::IsDead(oCNpc *this) ...not dead ...
            test eax, eax
            jg FILTER_NPC_ALIVE

            push ecx
            push edx
            push 1
            push 1
            lea ecx, [ebx + 0x668]      // ~ oCNpc.inventory   ...not empry inv
            mov eax, 0x0070D1A0         // int __thiscall oCNpcInventory::IsEmpty(oCNpcInventory *this, int useflags1, int useflags2)
            call eax
            test eax, eax
            pop edx
            pop ecx
            jnz CONTINUE_LOOP
            jmp VISIBLE

        FILTER_NPC_ALIVE :
            mov al, [ebx + 0x766]       // ~ int __thiscall oCNpc::IsHuman(oCNpc* this) ...not human
            cmp al, 16
            jle CONTINUE_LOOP
            mov eax, [ebx + 0x230]      // ~ oCNpc.guild  ...ignore sheep
            cmp eax, 18
            je CONTINUE_LOOP

        VISIBLE :
            push ecx
            push edx
            mov ecx, ebx
            call xr_filter
            test eax, eax
            pop edx
            pop ecx
            jz CONTINUE_LOOP
            mov xr_vobs.num, edx
            mov ebx, xr_ctx.ebx
            push 0x006C35A5             // HOOK_END
            ret

        LAST_ITER :
            mov xr_vobs.num, edx
            mov ebx, xr_ctx.ebx
            mov ecx, ds : 0x00AB2684      // oCNpc* oCNpc::player
            mov ds : 0x008B21E8, 1        // enable focs bar

            USE_ORIGINAL_FOCUS :             // ecx must be <oCNpc* player>
            push xr_real_get_focus
            ret
        }
    }

    // eax, ecx... unused/overwritten after both EXIT1 and EXIT2
    __declspec(naked) void xr_FakeExit()
    {
        __asm
        {
            mov al, xray_enabled
            test al, al
            jz DONE
            mov eax, xr_vobs.num
            test eax, eax
            js DONE
            mov eax, ds:0x008D7F94     // zCCamera::activeCam
            test eax, eax               // this totally a bug in the base game but they never found out
            jz DONE
            mov ecx, 0x006C35A0         // ecx is now <call Detoured>
            DONE :
            jmp ecx                     // ecx is xr_real_exit_1 or xr_real_exit_2
        }
    }
    __declspec(naked) void xr_FakeExit1()
    {
        __asm
        {
            mov ecx, xr_real_exit_1
            jmp xr_FakeExit
        }
    }
    __declspec(naked) void xr_FakeExit2()
    {
        __asm
        {
            mov ecx, xr_real_exit_2
            jmp xr_FakeExit
        }
    }

    bool __cdecl dummy_SetForegroundWindowEx(struct HWND__* hwnd);

    HOOK orig_SetForegroundWindowEx AS(0x00501F30, &dummy_SetForegroundWindowEx);

    bool __cdecl dummy_SetForegroundWindowEx(struct HWND__* hwnd) {
        return orig_SetForegroundWindowEx(hwnd);
    }

    __declspec(naked) void sysEvent_NoFocus()
    {
        __asm
        {
            mov eax, 0x5057A5
            jmp eax
        }
    }

    HOOK orig_sysEvent_Focus AS(0x505642, &sysEvent_NoFocus);


    /*
        COMMON
    */

    void Game_Entry() {
        //CreateConsole();
    }

    void Game_Init() {
        item_tool.w.onFocusChange = [&](bool active) {
            if (!active) orig_SetForegroundWindowEx(hWndApp);
        };
    }

    void Game_Exit() {
    }

    template<int KEY>
    bool WasSingleKeyJustPressed() {
        static int last = 0;
        int current = zinput->KeyPressed(KEY);
        bool justPressed = current && !last;
        last = current;
        return justPressed;
    }

    template<int KEY, int... MODIFIERS>
    bool WasKeyJustPressed() {
        // Check all modifiers
        bool modifiersHeld = true;
        using swallow = int[];
        (void)swallow {
            0, (modifiersHeld &= zinput->KeyPressed(MODIFIERS), 0)...
        };

        return WasSingleKeyJustPressed<KEY>() && modifiersHeld;
    }
}

namespace GOTHIC_ENGINE {
    void Game_Loop() {
        if (WasKeyJustPressed<KEY_L, KEY_LCONTROL, KEY_LSHIFT>()) {
			show_debug_rays = !!!!!show_debug_rays;
		}

        if (WasKeyJustPressed<KEY_X, KEY_LSHIFT>()) {
            xray_enabled = !!!!!xray_enabled;
        }

        if (WasKeyJustPressed<KEY_R, KEY_LCONTROL>()) {
            item_tool.open();
        }

        if (WasKeyJustPressed<KEY_T, KEY_LCONTROL>()) {
            script_canvas.open();
        }

        if (WasKeyJustPressed<KEY_F, KEY_LCONTROL>()) {
            player_stats.ShowPlayerStatsWindow();
        }

        if (show_debug_rays) showLootRays();

        item_tool.storeVisible();

        static int ld, lh, lm;
        int d, h, m;
        ogame->GetTime(d, h, m);
        if (d != ld || h != lh || m != lm) {
            ld = d; lh = h; lm = m;
            item_tool.RefreshLater();
            player_stats.refreshPlayerStats();
            //markJourney();
        }

        drinkIfCan();

		static float start = 0.0f, startFactor = 1.0f;
        bool up = zinput->KeyPressed(KEY_PGUP), down = zinput->KeyPressed(KEY_PGDN);
        bool justAdd = WasKeyJustPressed<KEY_PGUP>(), justSub = WasKeyJustPressed<KEY_PGDN>();
        if (up != down) {
            if (up ? justAdd : justSub) {
                start = ztimer->totalTimeFloatSecs;
				startFactor = ztimer->factorMotion;
            }
            else {
                ztimer->factorMotion
                    = startFactor
                    * std::pow(1.1f, (ztimer->totalTimeFloatSecs - start) * (up ? 1.0f : -1.0f));
            }
        }
        if (zinput->KeyPressed(KEY_HOME)) {
            ztimer->factorMotion = 1.0f;
            start = 0.0f;
            startFactor = 1.0f;
		}
    }

    void LoadEnd() {
        item_tool.RefreshLater();
    }
}

namespace GOTHIC_ENGINE {
  void Game_PreLoop() {
  }

  void Game_PostLoop() {
  }

  void Game_MenuLoop() {
  }

  // Information about current saving or loading world
  TSaveLoadGameInfo& SaveLoadGameInfo = UnionCore::SaveLoadGameInfo;

  void Game_SaveBegin() {
  }

  void Game_SaveEnd() {
  }

  void LoadBegin() {
  }

  void Game_LoadBegin_NewGame() {
    LoadBegin();
  }

  void Game_LoadEnd_NewGame() {
    LoadEnd();
  }

  void Game_LoadBegin_SaveGame() {
    LoadBegin();
  }

  void Game_LoadEnd_SaveGame() {
    LoadEnd();
  }

  void Game_LoadBegin_ChangeLevel() {
    LoadBegin();
  }

  void Game_LoadEnd_ChangeLevel() {
    LoadEnd();
  }

  void Game_LoadBegin_Trigger() {
  }
  
  void Game_LoadEnd_Trigger() {
  }
  
  void Game_Pause() {
  }
  
  void Game_Unpause() {
  }
  
  void Game_DefineExternals() {
  }

  void Game_ApplyOptions() {
  }

  /*
  Functions call order on Game initialization:
    - Game_Entry           * Gothic entry point
    - Game_DefineExternals * Define external script functions
    - Game_Init            * After DAT files init
  
  Functions call order on Change level:
    - Game_LoadBegin_Trigger     * Entry in trigger
    - Game_LoadEnd_Trigger       *
    - Game_Loop                  * Frame call window
    - Game_LoadBegin_ChangeLevel * Load begin
    - Game_SaveBegin             * Save previous level information
    - Game_SaveEnd               *
    - Game_LoadEnd_ChangeLevel   *
  
  Functions call order on Save game:
    - Game_Pause     * Open menu
    - Game_Unpause   * Click on save
    - Game_Loop      * Frame call window
    - Game_SaveBegin * Save begin
    - Game_SaveEnd   *
  
  Functions call order on Load game:
    - Game_Pause              * Open menu
    - Game_Unpause            * Click on load
    - Game_LoadBegin_SaveGame * Load begin
    - Game_LoadEnd_SaveGame   *
  */

#define AppDefault True
  CApplication* lpApplication = !CHECK_THIS_ENGINE ? Null : CApplication::CreateRefApplication(
    Enabled( AppDefault ) Game_Entry,
    Enabled( AppDefault ) Game_Init,
    Enabled( AppDefault ) Game_Exit,
    Enabled( AppDefault ) Game_PreLoop,
    Enabled( AppDefault ) Game_Loop,
    Enabled( AppDefault ) Game_PostLoop,
    Enabled( AppDefault ) Game_MenuLoop,
    Enabled( AppDefault ) Game_SaveBegin,
    Enabled( AppDefault ) Game_SaveEnd,
    Enabled( AppDefault ) Game_LoadBegin_NewGame,
    Enabled( AppDefault ) Game_LoadEnd_NewGame,
    Enabled( AppDefault ) Game_LoadBegin_SaveGame,
    Enabled( AppDefault ) Game_LoadEnd_SaveGame,
    Enabled( AppDefault ) Game_LoadBegin_ChangeLevel,
    Enabled( AppDefault ) Game_LoadEnd_ChangeLevel,
    Enabled( AppDefault ) Game_LoadBegin_Trigger,
    Enabled( AppDefault ) Game_LoadEnd_Trigger,
    Enabled( AppDefault ) Game_Pause,
    Enabled( AppDefault ) Game_Unpause,
    Enabled( AppDefault ) Game_DefineExternals,
    Enabled( AppDefault ) Game_ApplyOptions
  );
}

#endif