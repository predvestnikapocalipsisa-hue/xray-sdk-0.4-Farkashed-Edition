#include "../NavigationPreparation.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace SDKInGame;
    const auto node = [](float x, Vertex normal) {
        return NavigationNode{{x, 0, 0}, normal, {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}};
    };
    std::vector<NavigationNode> source{
        node(0, {0, 2, 0}), node(1, {0, 0, 0}), node(2, {0, -1, 0}),
        node(std::numeric_limits<float>::quiet_NaN(), {0, 1, 0}), node(4, {1, 0, 0})};
    source[0].links[0] = 2;
    source[0].links[1] = 1;
    source[2].links[0] = 0;
    source[2].links[1] = 999;
    const auto fixed = PrepareNavigation(source);
    assert(fixed.HasIssues());
    assert(fixed.invalidPositions == 1 && fixed.invalidNormals == 4 && fixed.invalidLinks == 1);
    assert(fixed.normalized == 2 && fixed.removed == 3 && fixed.disconnected == 2);
    assert(fixed.nodes.size() == 2 && fixed.nodes[0].normal.y == 1 && fixed.nodes[1].normal.y == 1);
    assert(fixed.nodes[0].links[0] == 1 && fixed.nodes[0].links[1] == UINT32_MAX);
    assert(fixed.nodes[1].links[0] == 0 && fixed.nodes[1].links[1] == UINT32_MAX);
    assert(source.size() == 5 && source[0].normal.y == 2 && source[0].links[0] == 2);
    assert(!PrepareNavigation(fixed.nodes).HasIssues());
    assert(PrepareNavigation({node(0, {0, 0, 0})}).nodes.empty());

    // Exercise the real wire validator: strict input is rejected and the repaired
    // snapshot is accepted with remapped indices, without weakening SceneView.
    SceneBuilder builder;
    builder.vertices = {{0,0,0}, {1,0,0}, {0,0,1}, {1,0,1}};
    builder.triangles = {{{0,1,2},0}, {{1,3,2},0}};
    builder.info.navigationCell = .7f;
    builder.navigation = source;
    SceneView view;
    const char* error = nullptr;
    auto bytes = builder.Finish();
    assert(!view.Open(bytes.data(), bytes.size(), error));
    builder.navigation = fixed.nodes;
    bytes = builder.Finish();
    assert(view.Open(bytes.data(), bytes.size(), error));
    assert(view.Info().navigationCount == 2 && view.GetNavigationNode(0).links[0] == 1);
    std::cout << "Navigation preparation tests passed\n";
}
