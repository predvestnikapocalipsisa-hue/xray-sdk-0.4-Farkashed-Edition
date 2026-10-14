#pragma once
#include "LevelRecovery.h"

namespace LevelRecovery { namespace NativeIO {
struct Instance
{
    std::string name, reference;
    Placement placement;
};
void SaveModel(const Level& level, const std::vector<TreePrototype>& parts, const std::string& file);
void SaveNormals(const std::vector<TreePrototype>& parts, const std::string& file);
bool EqualModel(const Level& level, const std::vector<TreePrototype>& parts, const std::string& file);
bool EqualNormals(const std::vector<TreePrototype>& parts, const std::string& file);
void SaveSelection(const std::vector<Instance>& instances, const std::string& file);
} }
