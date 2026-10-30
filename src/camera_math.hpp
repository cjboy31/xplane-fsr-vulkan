// SPDX-License-Identifier: MIT
#pragma once
#include "shared.hpp"
#include <algorithm>
namespace app {
// projection_matrix_3d can retain the forward OpenGL clip transform used by
// plugin drawing, while the intercepted Vulkan depth is reversed [0,1].
// Convert the projection BEFORE forming view/world/body reprojections. Changing
// only an FSR flag would leave distance and motion reconstruction inconsistent.
enum class ProjectionEncoding { Invalid, ForwardOpenGL, ReverseVulkan };
inline ProjectionEncoding vulkanProjection(float *out, const float *source) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(source[i]))
            return ProjectionEncoding::Invalid;
    if (std::abs(source[11] + 1.f) > 1e-5f || std::abs(source[15]) > 1e-7f ||
        std::abs(source[3]) > 1e-7f || std::abs(source[7]) > 1e-7f || source[0] <= 0 ||
        std::abs(source[5]) < 1e-7f)
        return ProjectionEncoding::Invalid;
    ProjectionEncoding encoding = ProjectionEncoding::Invalid;
    if (source[10] <= -1.f + 1e-6f && source[14] < 0)
        encoding = ProjectionEncoding::ForwardOpenGL;
    else if (source[10] >= -1e-7f && source[14] > 0)
        encoding = ProjectionEncoding::ReverseVulkan;
    if (encoding == ProjectionEncoding::Invalid)
        return encoding;
    float converted[16];
    memcpy(converted, source, sizeof(converted));
    if (encoding == ProjectionEncoding::ForwardOpenGL)
        // z_vk = (w_gl - z_gl) / 2; preserve X/Y and asymmetric frusta.
        for (int column = 0; column < 4; ++column)
            converted[4 * column + 2] = (source[4 * column + 3] - source[4 * column + 2]) * .5f;
    memcpy(out, converted, sizeof(converted));
    return encoding;
}
// Exact affine inverse in double precision. Transposing a nearly orthogonal
// float rotation amplifies its error at large simulator world coordinates.
inline bool inverseTransform(double *out, const double *m) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i]))
            return false;
    if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1)
        return inverse(out, m);
    double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6],
           i = m[10];
    double A = e * i - f * h, B = c * h - b * i, C = b * f - c * e;
    double D = f * g - d * i, E = a * i - c * g, F = c * d - a * f;
    double G = d * h - e * g, H = b * g - a * h, I = a * e - b * d;
    double determinant = a * A + b * D + c * G;
    double scale = std::max({std::abs(a), std::abs(b), std::abs(c), std::abs(d), std::abs(e),
                             std::abs(f), std::abs(g), std::abs(h), std::abs(i)});
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12 * scale * scale * scale)
        return inverse(out, m);
    double r[16] = {A, D, G, 0, B, E, H, 0, C, F, I, 0, 0, 0, 0, 1};
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            r[column * 4 + row] /= determinant;
    for (int row = 0; row < 3; ++row)
        r[12 + row] = -(r[row] * m[12] + r[4 + row] * m[13] + r[8 + row] * m[14]);
    for (double value : r)
        if (!std::isfinite(value))
            return false;
    memcpy(out, r, sizeof(r));
    return true;
}
struct ProjectionCache {
    float source[16]{};
    double matrix[16]{}, inverseMatrix[16]{};
    bool valid{};
    bool update(const float *input) {
        if (valid && !memcmp(source, input, sizeof(source)))
            return true;
        valid = false;
        for (int i = 0; i < 16; ++i) {
            if (!std::isfinite(input[i]))
                return false;
            matrix[i] = input[i];
        }
        if (!inverse(inverseMatrix, matrix))
            return false;
        for (double value : inverseMatrix)
            if (!std::isfinite(value))
                return false;
        memcpy(source, input, sizeof(source));
        valid = true;
        return true;
    }
};
} // namespace app
