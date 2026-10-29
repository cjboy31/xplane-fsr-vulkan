// SPDX-License-Identifier: MIT
#include "shared.hpp"
#include <iostream>
int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: status.exe XPLANE_PROCESS_ID\n";
        return 1;
    }
    auto name = app::mappingName(DWORD(strtoul(argv[1], nullptr, 10)));
    auto mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!mapping) {
        std::cerr << "Camera mapping unavailable (" << GetLastError() << ")\n";
        return 2;
    }
    auto *shared = static_cast<app::Shared *>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(app::Shared)));
    if (!shared) {
        CloseHandle(mapping);
        return 3;
    }
    app::Camera camera{};
    app::Telemetry status{};
    bool valid = app::readCamera(shared, camera);
    app::readStatus(shared, status);
    std::cout << "camera_valid=" << valid << " frame=" << camera.frame << " size=" << camera.width
              << 'x' << camera.height << " near=" << camera.nearPlane << " far=" << camera.farPlane
              << " fov=" << camera.verticalFov << " dt=" << camera.milliseconds
              << "\npresents=" << status.presents << " recordings=" << status.recordings
              << " bypasses=" << status.bypasses << " provider=" << status.provider
              << "\nreason=" << status.reason << '\n';
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    return 0;
}
