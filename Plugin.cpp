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
#include "ItemTool.h"
#include "PickPocket.h"
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

    /*
        QUICKER LOOT
    */

    HOOK Orig_oCNpcDoTakeVob PATCH(&oCNpc::DoTakeVob, &oCNpc::DoTakeVob_Hook);

    int oCNpc::DoTakeVob_Hook(zCVob* vob) {
        bool isPlayer = (this == ogame->GetSelfPlayerVob());

        zVEC3 wp;
        if (isPlayer && vob) {
            wp = vob->GetPositionWorld();
        }

        auto took = THISCALL(Orig_oCNpcDoTakeVob)(vob);

        if (isPlayer && took) {
            static constexpr auto RANGE = 500.0f;
            zTBBox3D bbox;
            bbox.mins[0] = wp[0] - RANGE;
            bbox.mins[1] = wp[1] - RANGE;
            bbox.mins[2] = wp[2] - RANGE;
            bbox.maxs[0] = wp[0] + RANGE;
            bbox.maxs[1] = wp[1] + RANGE;
            bbox.maxs[2] = wp[2] + RANGE;
            zCArray<zCVob*> surrounding;
            homeWorld->bspTree.bspRoot->CollectVobsInBBox3D(surrounding, bbox);
            for (int i = 0; i < surrounding.GetNum(); ++i) {
                auto otherVob = surrounding[i];
                if (otherVob == vob) continue; // skip self
                if (otherVob->_GetClassDef() != oCItem::classDef) // only items
                    continue;
                if (otherVob->GetPositionWorld().Distance(wp) > RANGE)
                    continue;
                auto* otherItem = static_cast<oCItem*>(otherVob);
                auto vobId = vob->GetObjectName();
                auto otherId = otherItem->GetObjectName();

                // plant collection, arrows, bolts, etc.
                constexpr char* patterms[] = { "ITPL_", "ITMI_GOLD", "ITLS_TORCH", "ITRW_ARROW", "ITRW_BOLT" };
                for (const char* pat : patterms) {
                    auto patlen = strlen(pat);
                    if (strncmp(vobId.ToChar(), pat, patlen) == 0 && strncmp(otherId.ToChar(), pat, patlen) == 0) {
                        DoPutInInventory(otherItem);
                        break; // only one match is needed
                    }
                }
            }
        }
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

    struct MapIcon {
        zCViewFX* view;
        ItemId id;

        MapIcon() = delete;

        MapIcon(ItemId id, zCViewObject* parent, zSTRING& iconTex) : id(id) {
            this->view = static_cast<zCViewFX*>(zCViewFX::_CreateNewInstance());
            this->view->Init(parent, true, 0, 0, 1.0f, 1.0f, iconTex);
            this->view->Open();
        }

        // Move constructor
        MapIcon(MapIcon&& other) noexcept
            : view(other.view), id(std::move(other.id)) {
            other.view = nullptr;
        }

        // Move assignment
        MapIcon& operator=(MapIcon&& other) noexcept {
            if (this != &other) {
                cleanupView();
                view = other.view;
                id = std::move(other.id);
                other.view = nullptr;
            }
            return *this;
        }

        // Delete copy constructor and copy assignment
        MapIcon(const MapIcon&) = delete;
        MapIcon& operator=(const MapIcon&) = delete;

        ~MapIcon() {
            cleanupView();
        }

    private:
        void cleanupView() {
            if (view) {
                view->Close();
                view->ViewParent->RemoveChild(view);
                view->Release();
                assert(view->refCtr <= 0 && "MapIcon view should be released before destruction");
                delete view;
                view = nullptr;
            }
        }
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

        mapIcons.clear();

        int IconW = ViewArrow.PixelSize.X;
        int IconH = ViewArrow.PixelSize.Y;
        zCViewObject* mapObj = ViewArrow.ViewParent;
        int mapX = mapObj->PixelPosition.X;
        int mapY = mapObj->PixelPosition.Y;
        int mapW = mapObj->PixelSize.X;
        int mapH = mapObj->PixelSize.Y;
        float worldMinX = LevelCoords[0]; // dword214
        float worldMinZ = LevelCoords[1];    // dword214
        float worldMaxX = LevelCoords[2];    // dword218
        float worldMaxZ = LevelCoords[3];    // dword21C
        float worldW = worldMaxX - worldMinX;
        float worldH = worldMaxZ - worldMinZ;
        if (worldW == 0.0f || worldH == 0.0f)
            return;
        float invW = 1.0f / worldW;
        float invH = 1.0f / worldH;

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

            icon->view->TextureColor = (GetColorForBuffType(type));
            zCPosition size; size.X = IconW; size.Y = IconH;
            icon->view->SetPixelSize(size);

            float nx = (wp[0] - worldMinX) * invW;
            float nz = (wp[2] - worldMinZ) * invH;
            int px = int(mapX + nx * mapW) - IconW / 2;
            int py = int(mapY + nz * mapH) - IconH / 2;

            zCPosition pos; pos.X = px; pos.Y = py;
            icon->view->SetPixelPosition(pos);
            icon->view->SetTexture(*iconTex);
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
        zSTRING currentWorld;
        int selectedRow = -1;
        bool needRefresh = false;

        static inline std::string wToStr(const wchar_t* wchars) {
            std::string codeStr;
            while (wchars && *wchars)
                codeStr.push_back(static_cast<char>(*(wchars++)));
            return codeStr;
        }

        zSTRING namePls(int row) {
            if (row > (int)tableResults.size() || row < 0)
                return "";
            auto res = tableResults[row];
            return wToStr(insertCodes.view(0, *res)).c_str();
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

                auto ite = resultsByItemName.find(wToStr(insertCodes.view(0, *entryPtr)).c_str());
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
                            resultsByItemName[wToStr(wchars).c_str()] = { entryPtr, 0 };
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

            w.Open();
        }

        /* returns worldMatchesMap */
        bool updateMap() {
            auto wld = ogame->GetGameWorld();
            auto worldFile = wld->GetWorldFilename();
            if (currentWorld == worldFile) return true;
            currentWorld = worldFile;

            static constexpr struct {
                const char* mapTGA;
                const char* worldZen;
                float minX, minZ, maxX, maxZ;
            } maps[] = {
                { "Map_NewWorld.tga", "NewWorld\\NewWorld.zen", -28000.0f, 50500.0f, 95500.0f, -42500.0f },
                { "Map_OldWorld.tga", "OldWorld\\OldWorld.zen", -78500.0f, 47500.0f, 54000.0f, -53000.0f },
                { "Map_AddonWorld_Treasures.tga", "Addon\\AddonWorld.zen", -47783.0f, 36300.0f, 43949.0f, -32300.0f }
            };
            for (const auto& map : maps) {
                if (_stricmp(worldFile, map.worldZen) == 0) {
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
    PlayerStatsWindow ps;

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

    bool __cdecl dummy_SetForegroundWindowEx(struct HWND__ * hwnd) {
        return 0;
    }

    HOOK orig_SetForegroundWindowEx AS(0x00501F30, &dummy_SetForegroundWindowEx);

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
        if (WasKeyJustPressed<KEY_X, KEY_LSHIFT>()) {
            xray_enabled = !!!!!xray_enabled;
        }

        if (WasKeyJustPressed<KEY_T, KEY_LSHIFT>()) {
            item_tool.open();
        }

        if (WasKeyJustPressed<KEY_B, KEY_LCONTROL>()) {
            ps.ShowPlayerStatsWindow();
        }

        item_tool.storeVisible();

        static int ld, lh, lm;
        int d, h, m;
        ogame->GetTime(d, h, m);
        if (d != ld || h != lh || m != lm) {
            ld = d; lh = h; lm = m;
            item_tool.RefreshLater();
            ps.refreshPlayerStats();
        }

        drinkIfCan();
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