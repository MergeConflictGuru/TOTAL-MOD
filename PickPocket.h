#ifndef __PPKT_H__
#define __PPKT_H__

// Define a simple struct with plain C-strings
typedef struct {
    const char* npc;
    const char* item;
} NPCItem;

// Initialize the array (all caps strings)
NPCItem npcPickpocketItems[] = {
    { "KDF_508_GORAX",    "ITKE_KLOSTERSCHATZ" },
    { "PC_MAGE_OW",       "ITPO_PERM_MANA"     },
    { "VLK_409_ZURIS",    "ITPO_HEALTH_03"     },
    { "VLK_422_SALANDRIL","ITKE_SALANDRIL"     }
};

// If you need the count:
const size_t npcItemCount = sizeof(npcPickpocketItems) / sizeof(npcPickpocketItems[0]);

#endif