#include "api/utils/FileUtils.hpp"
#include "api/utils/StringUtils.hpp"
#include <filesystem>

namespace utils {
namespace fs = std::filesystem;

std::optional<std::string> readFile(fs::path const& filePath, bool isBinary) {
    if (!fs::exists(filePath)) {
        return std::nullopt;
    }
    std::ifstream           fRead;
    std::ios_base::openmode mode = std::ios_base::in;
    if (isBinary) mode |= std::ios_base::binary;
    fRead.open(filePath, mode);
    if (!fRead.is_open()) {
        return std::nullopt;
    }
    std::string data((std::istreambuf_iterator<char>(fRead)), {});
    fRead.close();
    return data;
}

bool writeFile(fs::path const& filePath, std::string_view content, bool isBinary) {
    std::ofstream           fWrite;
    std::ios_base::openmode mode = std::ios_base::out;
    if (isBinary) mode |= std::ios_base::binary;
    if (!fs::exists(filePath.parent_path())) {
        fs::create_directories(filePath.parent_path());
    }
    fWrite.open(filePath, mode);
    if (!fWrite.is_open()) {
        return false;
    }
    fWrite << content;
    fWrite.close();
    return true;
}

std::optional<std::vector<uint8_t>> readBinaryFile(std::filesystem::path const& filePath) {
    if (auto data = readFile(filePath, true)) {
        return utils::toBinaryArray(data.value());
    }
    return std::nullopt;
}

bool writeBinaryFile(std::filesystem::path const& filePath, std::vector<uint8_t> const& content) {
    auto binary = utils::toBinaryString(content);
    return writeFile(filePath, binary, true);
}

std::vector<std::string> getAllFileFullNameInDirectory(std::filesystem::path const& path) {
    std::vector<std::string> fileNames;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            fileNames.push_back(entry.path().filename().string());
        }
    }
    return fileNames;
}

std::vector<std::filesystem::path> getAllFileDirectories(std::filesystem::path const& path) {
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_directory()) {
            result.push_back(entry.path());
        }
    }
    return result;
}

} // namespace utils
