#pragma once
// Project persistence and the project browser index.

#include "model/Document.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ppn {

struct ProjectInfo {
    std::string name;
    std::filesystem::path path;
    std::string modified;
};

bool saveProject(const std::filesystem::path& path, const Document& doc, std::string& error);
bool loadProject(const std::filesystem::path& path, Document& doc, std::string& error);
std::vector<ProjectInfo> listProjects(const std::filesystem::path& folder);
bool deleteProject(const std::filesystem::path& path, std::string& error);

bool validProjectName(const std::string& name, std::string* reason = nullptr);

} // namespace ppn
