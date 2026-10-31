// SPDX-License-Identifier: MIT
// Local model data only. The selected NVIDIA DLL is never loaded or distributed.
#pragma once
#include "nr_models.hpp"
#include "runtime_integrity.hpp"
#include "settings_lock.hpp"
#include <array>
namespace xpfsr::nr {
inline std::wstring modelImportLock(const std::filesystem::path &root) {
    return app::lockName(std::filesystem::weakly_canonical(root).wstring()) + L"-nr-models";
}
inline int installExtractedModels(const std::filesystem::path &root,
                                  const std::filesystem::path &staging, bool replaceExisting) {
    app::NamedLock lock(modelImportLock(root));
    const auto data = root / L"nr/dlssnr-amd";
    struct ModelFile { std::filesystem::path source, target; std::string digest; bool exists{}; };
    std::vector<ModelFile> models;
    // Validate the entire set before publishing any file. In-sim imports only
    // add missing models: a context may already be reading an existing model.
    for (auto name : {L"dlssnr-small.bin", L"dlssnr.bin"}) {
        const auto source = staging / name, target = data / name;
        if (!std::filesystem::is_regular_file(source)) continue;
        auto digest = app::fileHash(source);
        if (!acceptedModel(!wcscmp(name, L"dlssnr-small.bin"), std::filesystem::file_size(source), digest))
            throw std::runtime_error("Extracted model failed verification; no model was installed");
        bool exists = std::filesystem::exists(target);
        if (exists && app::fileHash(target) != digest && !replaceExisting)
            throw std::runtime_error("An existing model differs. Close X-Plane and use the optional desktop model importer to replace it safely");
        models.push_back({source, target, std::move(digest), exists});
    }
    if (models.empty()) throw std::runtime_error("The importer did not produce a supported model");
    std::filesystem::create_directories(data);
    for (auto &model : models) {
        bool same = model.exists && app::fileHash(model.target) == model.digest;
        if (model.exists && !same) {
            auto old = app::fileHash(model.target);
            auto backup = model.target; backup += L".previous-" + std::wstring(old.begin(), old.end()) + L".bin";
            if (!std::filesystem::exists(backup)) std::filesystem::copy_file(model.target, backup);
            if (app::fileHash(backup) != old) throw std::runtime_error("Previous model backup verification failed");
        }
        if (!same && !MoveFileExW(model.source.c_str(), model.target.c_str(),
                MOVEFILE_WRITE_THROUGH | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0)))
            throw std::runtime_error("Could not install the verified model; existing data was retained");
        if (same) std::filesystem::remove(model.source);
        auto digest = std::wstring(model.digest.begin(), model.digest.end());
        if (!launcher::writeSetting(L"Models", model.target.filename().c_str(), digest.c_str(),
                                    (root / L"nr/USER-MODELS.ini").c_str()))
            throw std::runtime_error("Model installed, but its verification record could not be saved");
    }
    return int(models.size());
}
inline bool validImportToken(const std::wstring &token) {
    return token.size() == 32 && token.find_first_not_of(L"0123456789abcdef") == std::wstring::npos;
}
inline std::filesystem::path modelImportStatus(const std::filesystem::path &root, const std::wstring &job) {
    if (!validImportToken(job)) throw std::runtime_error("Invalid model import job");
    return root / L"logs" / (L"nr-import-" + job + L".txt");
}
struct ModelImportProgress { std::string phase, message; };
inline ModelImportProgress readModelImportProgress(const std::filesystem::path &root, const std::wstring &job) {
    std::ifstream in(modelImportStatus(root, job), std::ios::binary);
    char buffer[2049]{}; in.read(buffer, 2048);
    std::istringstream text(std::string(buffer, size_t(in.gcount())));
    std::string magic, token; ModelImportProgress result;
    if (!(text >> magic >> token) || magic != "XPFSR24_NR_IMPORT_V1" || token != std::string(job.begin(), job.end())) return {};
    text.ignore(1024, '\n'); std::getline(text, result.phase); std::getline(text, result.message);
    if (result.phase != "selecting" && result.phase != "extracting" && result.phase != "verifying" &&
        result.phase != "success" && result.phase != "cancelled" && result.phase != "error") return {};
    return result;
}
inline void writeModelImportProgress(const std::filesystem::path &root, const std::wstring &job,
                                     const std::string &phase, const std::string &message) {
    auto target = modelImportStatus(root, job), temp = target; temp += L".tmp";
    std::filesystem::create_directories(target.parent_path());
    { std::ofstream out(temp, std::ios::binary); out << "XPFSR24_NR_IMPORT_V1 " << std::string(job.begin(), job.end())
        << '\n' << phase << '\n' << message.substr(0, 1500) << '\n';
      if (!out) throw std::runtime_error("Cannot write model import status"); }
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish model import status");
}
} // namespace xpfsr::nr
