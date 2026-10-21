#pragma once
#include "SceneProtocol.h"
#include <windows.h>
#include <string>

namespace SDKInGame
{
    class SharedScene
    {
        HANDLE mapping = nullptr;
        void* view = nullptr;
        size_t bytes = 0;
    public:
        SharedScene() = default;
        SharedScene(const SharedScene&) = delete;
        SharedScene& operator=(const SharedScene&) = delete;
        ~SharedScene() { Close(); }
        void Close()
        {
            if (view) UnmapViewOfFile(view);
            if (mapping) CloseHandle(mapping);
            mapping = nullptr; view = nullptr; bytes = 0;
        }
        bool Create(const std::wstring& name, const std::vector<uint8_t>& scene)
        {
            Close();
            SceneView check; const char* error;
            if (!check.Open(scene.data(), scene.size(), error)) return false;
            const uint64_t length = scene.size();
            mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                DWORD(length >> 32), DWORD(length), name.c_str());
            if (!mapping || GetLastError() == ERROR_ALREADY_EXISTS) { Close(); return false; }
            view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, scene.size());
            if (!view) { Close(); return false; }
            std::memcpy(view, scene.data(), scene.size());
            bytes = scene.size();
            return true;
        }
        bool Open(const std::wstring& name)
        {
            Close();
            mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
            if (!mapping) return false;
            view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(Header));
            if (!view) { Close(); return false; }
            Header header; std::memcpy(&header, view, sizeof(header));
            UnmapViewOfFile(view); view = nullptr;
            if (header.bytes < sizeof(Header) || header.bytes > maxSceneBytes) { Close(); return false; }
            view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, size_t(header.bytes));
            if (!view) { Close(); return false; }
            bytes = size_t(header.bytes);
            return true;
        }
        const void* Data() const { return view; }
        size_t Size() const { return bytes; }
    };
}
