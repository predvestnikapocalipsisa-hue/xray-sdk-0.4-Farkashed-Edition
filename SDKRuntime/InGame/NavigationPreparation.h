#pragma once
#include "SceneProtocol.h"
#include <string>

namespace SDKInGame
{
    // Export-only repair. Never relax SceneView's wire validation or mutate the SDK map.
    struct NavigationPreparation
    {
        std::vector<NavigationNode> nodes;
        size_t invalidPositions = 0, invalidNormals = 0, invalidLinks = 0;
        size_t normalized = 0, removed = 0, disconnected = 0;
        bool HasIssues() const { return invalidPositions || invalidNormals || invalidLinks; }
        std::string Report() const
        {
            return "Navigation diagnostics:\nInvalid positions: " + std::to_string(invalidPositions) +
                "\nInvalid normals: " + std::to_string(invalidNormals) +
                "\nInvalid links: " + std::to_string(invalidLinks) +
                "\n\nSnapshot repair: " + std::to_string(normalized) + " normals normalized, " +
                std::to_string(removed) + " nodes omitted, " + std::to_string(disconnected) +
                " links disconnected.\nRemaining nodes: " + std::to_string(nodes.size()) +
                "\nThe original SDK AI Map is unchanged. Omitted nodes can leave gaps in navigation.\n";
        }
    };

    inline NavigationPreparation PrepareNavigation(const std::vector<NavigationNode>& source)
    {
        NavigationPreparation result;
        std::vector<uint32_t> indices(source.size(), UINT32_MAX);
        for (size_t i = 0; i < source.size(); ++i)
        {
            auto node = source[i];
            const double length = double(node.normal.x)*node.normal.x +
                double(node.normal.y)*node.normal.y + double(node.normal.z)*node.normal.z;
            const bool badPosition = !Finite(node.position);
            const bool badNormal = !Finite(node.normal) || node.normal.y < .1f || length < .5 || length > 2.;
            result.invalidPositions += badPosition;
            result.invalidNormals += badNormal;
            for (auto link : node.links)
                if (link != UINT32_MAX && link >= source.size()) ++result.invalidLinks;
            bool usable = !badPosition && Finite(node.normal) && length > 1.e-20;
            if (usable && badNormal)
            {
                // A plane and its negation describe the same surface; OGSR needs the upward normal.
                const double scale = (node.normal.y < 0 ? -1. : 1.) / std::sqrt(length);
                node.normal = {float(node.normal.x*scale), float(node.normal.y*scale), float(node.normal.z*scale)};
                usable = node.normal.y >= .1f;
                if (usable) ++result.normalized;
            }
            if (!usable) { ++result.removed; continue; }
            indices[i] = uint32_t(result.nodes.size());
            result.nodes.push_back(node);
        }
        for (auto& node : result.nodes)
            for (auto& link : node.links)
            {
                if (link == UINT32_MAX) continue;
                const uint32_t next = link < indices.size() ? indices[link] : UINT32_MAX;
                if (next == UINT32_MAX) ++result.disconnected;
                link = next;
            }
        return result;
    }
}
