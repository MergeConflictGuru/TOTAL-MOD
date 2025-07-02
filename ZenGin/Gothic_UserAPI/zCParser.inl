// Supported with union (c) 2020 Union team

// User API for zCParser
// Add your methods here


void* __cdecl CallFunc_Hook_ByIndex(int index, ...);
void* CallFunc_Hook_ByName(Gothic_II_Addon::zSTRING const& funcName);
void Error_Hooked(Gothic_II_Addon::zSTRING&, int);