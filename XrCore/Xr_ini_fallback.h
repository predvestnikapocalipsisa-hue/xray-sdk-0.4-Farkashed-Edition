#pragma once

// Installs a fallback for missing sections of pSettings on the editor thread.
// The provider owns returned sections and keeps them alive until unregistered.
using IniSectionFallback = CInifile::Sect* (*)(LPCSTR section);
XRCORE_API void SetIniSectionFallback(IniSectionFallback lookup);
