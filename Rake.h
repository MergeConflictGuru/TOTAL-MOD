#ifndef __RAKE_H__
#define __RAKE_H__

/*
    RAKEPLACE TABLE
*/

/*
Convert the input script to a C structure for rake treasure locations by:
1. Identifying all condition blocks that check:
   - heroes distance to a waypoint
   - `RAKEPLACE[<INDEX>] == FALSE` (or equivalent false check)
   - One or more InsertItem calls
   - `RAKEPLACE[<INDEX>] = TRUE` assignment

2. For each valid block, extract:
   - WAYPOINT: Name of the waypoint
   - INDEX: Integer inside `RAKEPLACE[...]`
   - ITEMS: First arg of `Wld_InsertItem()` or similar, convert to ALL CAPS

3. Generate C output EXACTLY in this format:
// AUTO-GENERATED - DO NOT EDIT

#define RAKEPLACE_MAX_ITEMS <MAX_ITEMS>  // Smallest number that fits all items

typedef struct {
    const char* waypoint;
    unsigned index;
	const char* items[RAKEPLACE_MAX_ITEMS]; //NAMES IN ALL CAPS
    unsigned item_count;
} RakeTreasureSpot;

const RakeTreasureSpot RakeTreasureTable[] = {
    // Entries in order of discovery
    { "<WAYPOINT>", <INDEX>, { <ITEM1>, <ITEM2>, ... }, <ITEM_COUNT> },
    // ... other entries ...
};

const unsigned RakeplaceTableSize = sizeof(RakeplaceTable) / sizeof(RakeplaceTable[0]);

// END of AUTO-GENERATED
*/

// AUTO-GENERATED - DO NOT EDIT

#define RAKEPLACE_MAX_ITEMS 3  // Smallest number that fits all items

typedef struct {
    const char* waypoint;
    unsigned index;
    const char* items[RAKEPLACE_MAX_ITEMS];
    unsigned item_count;
} RakeTreasureSpot;

constexpr RakeTreasureSpot RakeTreasureTable[] = {
    { "NW_BIGFARM_LAKE_CAVE_07", 1, { "ITSE_GOLDPOCKET25" }, 1 },
    { "NW_LAKE_GREG_TREASURE_01", 2, { "ITSE_GOLDPOCKET100" }, 1 },
    { "NW_FARM3_GREGTREASURE_01", 3, { "ITMI_GOLDCUP" }, 1 },
    { "NW_FARM3_MOUNTAINLAKE_MONSTER_01", 4, { "ITMI_SILVERCHALICE" }, 1 },
    { "NW_BIGMILL_FARM3_01", 5, { "ITAM_PROT_POINT_01" }, 1 },
    { "ADW_ENTRANCE_RAKEPLACE_01", 12, { "ITWR_STONEPLATECOMMON_ADDON", "ITMI_SILVERCHALICE" }, 2 },
    { "ADW_ENTRANCE_RAKEPLACE_02", 13, { "ITWR_DEXSTONEPLATE1_ADDON", "ITMI_GOLDCUP" }, 2 },
    { "ADW_ENTRANCE_RAKEPLACE_03", 14, { "ITPO_PERM_HEALTH", "ITSE_GOLDPOCKET100" }, 2 },
    { "ADW_ENTRANCE_RAKEPLACE_04", 15, { "ITMI_SILVERRING", "ITMW_SCHWERT4" }, 2 },
    { "ADW_VALLEY_GREGTREASURE_01", 16, { "ITSE_GOLDPOCKET100", "ITPO_HEALTH_02", "ITPO_MANA_03" }, 3 },
    { "ADW_VALLEY_RAKEPLACE_01", 17, { "ITPO_MANA_ADDON_04", "ITPO_SPEED", "ITPO_MANA_02" }, 3 },
    { "ADW_VALLEY_RAKEPLACE_02", 18, { "ITPO_HEALTH_ADDON_04", "ITWR_STONEPLATECOMMON_ADDON", "ITSE_LOCKPICKFISCH" }, 3 },
    { "ADW_VALLEY_RAKEPLACE_03", 19, { "ITSC_FIRERAIN", "ITSE_GOLDPOCKET50", "ITWR_STONEPLATECOMMON_ADDON" }, 3 },
    { "ADW_BANDITSCAMP_RAKEPLACE_01", 20, { "ITMI_HONIGTABAK", "ITWR_STONEPLATECOMMON_ADDON", "ITAM_ADDON_MANA" }, 3 },
    { "ADW_BANDITSCAMP_RAKEPLACE_02", 21, { "ITSC_SUMGOBSKEL", "ITPO_MANA_03" }, 2 },
    { "ADW_BANDITSCAMP_RAKEPLACE_03", 22, { "ITSC_TRFSHADOWBEAST", "ITSC_LIGHTHEAL" }, 2 },
    { "ADW_BANDITSCAMP_RAKEPLACE_04", 23, { "ITWR_STONEPLATECOMMON_ADDON", "ITRI_HP_01" }, 2 },
    { "ADW_CANYON_MINE1_11", 24, { "ITSE_ADDON_FRANCISCHEST" }, 1 },
    { "ADW_CANYON_RAKEPLACE_01", 25, { "ITMI_RUNEBLANK", "ITSE_GOLDPOCKET25" }, 2 },
    { "ADW_CANYON_RAKEPLACE_02", 26, { "ITMI_NUGGET", "ITSC_LIGHTNINGFLASH", "ITSC_CHARGEFIREBALL" }, 3 },
    { "ADW_CANYON_RAKEPLACE_03", 27, { "ITSE_GOLDPOCKET25", "ITWR_MANASTONEPLATE1_ADDON", "ITMI_PITCH" }, 3 },
    { "ADW_CANYON_RAKEPLACE_04", 28, { "ITMI_SILVERRING", "ITMI_SULFUR", "ITWR_TWOHSTONEPLATE1_ADDON" }, 3 },
    { "ADW_CANYON_RAKEPLACE_05", 29, { "ITMI_GOLDRING", "ITAT_DRAGONBLOOD" }, 2 },
    { "ADW_PIRATECAMP_GREGTREASURE_KOMPASS", 30, { "ITMI_ADDON_KOMPASS_MIS" }, 1 },
};

const unsigned RakeplaceTableSize = sizeof(RakeTreasureTable) / sizeof(RakeTreasureTable[0]);

// END of AUTO-GENERATED

#endif // __RAKE_H__