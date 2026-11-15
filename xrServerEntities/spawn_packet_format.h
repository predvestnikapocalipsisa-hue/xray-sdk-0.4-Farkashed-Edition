#pragma once

extern thread_local bool compiledCopSpawnPacket;
inline bool UseSocSpawnPacket() { return Core.SocSdk && !compiledCopSpawnPacket; }
