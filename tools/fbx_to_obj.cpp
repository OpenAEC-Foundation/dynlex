#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ufbx.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

namespace fs = std::filesystem;

struct Bounds {
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double min_z = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();
    double max_z = -std::numeric_limits<double>::infinity();

    void include(const ufbx_vec3 &p)
    {
        min_x = std::min(min_x, p.x);
        min_y = std::min(min_y, p.y);
        min_z = std::min(min_z, p.z);
        max_x = std::max(max_x, p.x);
        max_y = std::max(max_y, p.y);
        max_z = std::max(max_z, p.z);
    }

    [[nodiscard]] bool valid() const
    {
        return std::isfinite(min_x) && std::isfinite(min_y) && std::isfinite(min_z) && std::isfinite(max_x)
            && std::isfinite(max_y) && std::isfinite(max_z);
    }
};

struct MaterialOutput {
    std::string safe_name;
    std::string debug_name;
    double kd_r = 0.8;
    double kd_g = 0.8;
    double kd_b = 0.8;
    bool has_texture = false;
    std::string texture_source;
    std::string texture_ppm_abs;
};

std::string to_string(ufbx_string str)
{
    if (!str.data || str.length == 0) {
        return "unnamed";
    }
    return std::string(str.data, str.length);
}

std::string to_raw_string(ufbx_string str)
{
    if (!str.data || str.length == 0) {
        return {};
    }
    return std::string(str.data, str.length);
}

std::string to_lower_ascii(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string trim_ascii(std::string_view text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

bool contains_ascii_nocase(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) {
        return true;
    }
    if (haystack.size() < needle.size()) {
        return false;
    }
    const std::string haystack_lower = to_lower_ascii(haystack);
    const std::string needle_lower = to_lower_ascii(needle);
    return haystack_lower.find(needle_lower) != std::string::npos;
}

void print_usage(const char *argv0)
{
    std::cerr << "Usage: " << argv0 << " <input.fbx> <output.obj> [--triangle-step N]\n";
}

bool parse_positive_u64(const char *text, uint64_t &value_out)
{
    if (!text || *text == '\0') {
        return false;
    }

    uint64_t value = 0;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        const uint64_t digit = static_cast<uint64_t>(*p - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }

    if (value == 0) {
        return false;
    }

    value_out = value;
    return true;
}

std::string sanitize_name(std::string_view input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == '.') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }

    if (out.empty()) {
        out = "material";
    }
    return out;
}

const ufbx_texture *pick_color_texture(const ufbx_material *material)
{
    if (!material) {
        return nullptr;
    }

    auto pick_map = [](const ufbx_material_map &map) -> const ufbx_texture * {
        if (map.texture_enabled && map.texture) {
            return map.texture;
        }
        return nullptr;
    };

    if (const ufbx_texture *texture = pick_map(material->pbr.base_color)) {
        return texture;
    }
    if (const ufbx_texture *texture = pick_map(material->fbx.diffuse_color)) {
        return texture;
    }
    if (const ufbx_texture *texture = ufbx_find_prop_texture(material, "DiffuseColor")) {
        return texture;
    }
    if (const ufbx_texture *texture = ufbx_find_prop_texture(material, "BaseColor")) {
        return texture;
    }

    for (size_t i = 0; i < material->textures.count; ++i) {
        const ufbx_material_texture &entry = material->textures.data[i];
        if (!entry.texture) {
            continue;
        }
        const std::string prop_name = to_raw_string(entry.material_prop);
        if (contains_ascii_nocase(prop_name, "basecolor") || contains_ascii_nocase(prop_name, "diffuse")
            || contains_ascii_nocase(prop_name, "albedo") || contains_ascii_nocase(prop_name, "color")) {
            return entry.texture;
        }
    }

    for (size_t i = 0; i < material->textures.count; ++i) {
        const ufbx_material_texture &entry = material->textures.data[i];
        if (entry.texture) {
            return entry.texture;
        }
    }

    return nullptr;
}

void pick_diffuse_color(const ufbx_material *material, double &r, double &g, double &b)
{
    r = 0.8;
    g = 0.8;
    b = 0.8;

    if (!material) {
        return;
    }

    auto pick = [&](const ufbx_material_map &map) {
        if (map.has_value && map.value_components >= 3) {
            r = map.value_vec3.x;
            g = map.value_vec3.y;
            b = map.value_vec3.z;
            return true;
        }
        return false;
    };

    if (!pick(material->pbr.base_color)) {
        pick(material->fbx.diffuse_color);
    }

    if (r == 0.8 && g == 0.8 && b == 0.8) {
        ufbx_vec3 kDefault{};
        kDefault.x = 0.8f;
        kDefault.y = 0.8f;
        kDefault.z = 0.8f;
        const ufbx_vec3 diffuse_from_props = ufbx_find_vec3(&material->props, "DiffuseColor", kDefault);
        if (diffuse_from_props.x != kDefault.x || diffuse_from_props.y != kDefault.y || diffuse_from_props.z != kDefault.z) {
            r = diffuse_from_props.x;
            g = diffuse_from_props.y;
            b = diffuse_from_props.z;
        } else {
            const ufbx_vec3 base_from_props = ufbx_find_vec3(&material->props, "BaseColor", kDefault);
            if (base_from_props.x != kDefault.x || base_from_props.y != kDefault.y || base_from_props.z != kDefault.z) {
                r = base_from_props.x;
                g = base_from_props.y;
                b = base_from_props.z;
            } else {
                const ufbx_vec3 maya_base = ufbx_find_vec3(&material->props, "Maya|baseColor", kDefault);
                if (maya_base.x != kDefault.x || maya_base.y != kDefault.y || maya_base.z != kDefault.z) {
                    r = maya_base.x;
                    g = maya_base.y;
                    b = maya_base.z;
                }
            }
        }
    }

    r = std::clamp(r, 0.0, 1.0);
    g = std::clamp(g, 0.0, 1.0);
    b = std::clamp(b, 0.0, 1.0);
}

bool load_texture_rgb(const ufbx_texture *texture, const fs::path &resolved_source, int &width, int &height, std::vector<uint8_t> &rgb)
{
    width = 0;
    height = 0;
    rgb.clear();

    int channels = 0;
    stbi_uc *pixels = nullptr;

    // Keep OBJ UV convention (origin at lower-left): flip source rows while decoding.
    stbi_set_flip_vertically_on_load(1);

    if (texture && texture->content.size > 0 && texture->content.data) {
        pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc *>(texture->content.data), static_cast<int>(texture->content.size), &width, &height,
            &channels, 3
        );
    }

    if (!pixels && texture) {
        for (size_t i = 0; i < texture->file_textures.count; ++i) {
            const ufbx_texture *file_texture = texture->file_textures.data[i];
            if (!file_texture) {
                continue;
            }
            if (file_texture->content.size > 0 && file_texture->content.data) {
                pixels = stbi_load_from_memory(
                    reinterpret_cast<const stbi_uc *>(file_texture->content.data),
                    static_cast<int>(file_texture->content.size), &width, &height, &channels, 3
                );
                if (pixels) {
                    break;
                }
            }
        }
    }

    if (!pixels && !resolved_source.empty() && fs::exists(resolved_source)) {
        pixels = stbi_load(resolved_source.string().c_str(), &width, &height, &channels, 3);
    }

    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const size_t byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    rgb.assign(pixels, pixels + byte_count);
    stbi_image_free(pixels);
    return true;
}

bool write_ppm(const fs::path &path, int width, int height, const std::vector<uint8_t> &rgb)
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    const size_t expected_size = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    if (rgb.size() != expected_size) {
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    out << "P6\n" << width << " " << height << "\n255\n";
    out.write(reinterpret_cast<const char *>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    out.flush();
    return static_cast<bool>(out);
}

fs::path pick_texture_source_path(
    const ufbx_texture *texture, const fs::path &input_dir, std::string_view material_debug_name,
    std::string_view material_safe_name
)
{
    auto expand_texture_tokens = [](std::string raw) {
        const std::string token = "[texture]";
        const std::string replacement = "../textures";
        size_t pos = 0;
        while ((pos = raw.find(token, pos)) != std::string::npos) {
            raw.replace(pos, token.size(), replacement);
            pos += replacement.size();
        }
        std::replace(raw.begin(), raw.end(), '\\', '/');
        return raw;
    };

    auto find_existing_candidate = [&](const std::string &raw_path_text) -> std::optional<fs::path> {
        if (raw_path_text.empty()) {
            return std::nullopt;
        }

        const std::string expanded_text = expand_texture_tokens(raw_path_text);
        std::vector<fs::path> candidates;
        candidates.reserve(4);

        fs::path raw_path(expanded_text);
        candidates.push_back(raw_path);
        if (!raw_path.is_absolute()) {
            candidates.push_back(input_dir / raw_path);
        }

        std::error_code ec;
        for (const fs::path &candidate : candidates) {
            const fs::path normalized = candidate.lexically_normal();
            ec.clear();
            if (!normalized.empty() && fs::exists(normalized, ec) && !ec) {
                return fs::absolute(normalized);
            }
        }

        return std::nullopt;
    };

    auto find_texture_by_stem = [&](const fs::path &requested_name) -> std::optional<fs::path> {
        const std::string wanted_filename = to_lower_ascii(requested_name.filename().string());
        const std::string wanted_stem = to_lower_ascii(requested_name.stem().string());
        if (wanted_filename.empty() && wanted_stem.empty()) {
            return std::nullopt;
        }

        const fs::path texture_root = input_dir.parent_path() / "textures";
        std::error_code ec;
        if (!fs::exists(texture_root, ec) || ec) {
            return std::nullopt;
        }

        std::optional<fs::path> stem_match;
        for (const fs::directory_entry &entry : fs::recursive_directory_iterator(
                 texture_root, fs::directory_options::skip_permission_denied, ec
             )) {
            if (ec) {
                continue;
            }
            if (!entry.is_regular_file(ec) || ec) {
                continue;
            }

            const fs::path entry_path = entry.path();
            const std::string entry_filename = to_lower_ascii(entry_path.filename().string());
            if (!wanted_filename.empty() && entry_filename == wanted_filename) {
                return fs::absolute(entry_path);
            }

            if (!stem_match.has_value()) {
                const std::string entry_stem = to_lower_ascii(entry_path.stem().string());
                if (!wanted_stem.empty() && entry_stem == wanted_stem) {
                    stem_match = fs::absolute(entry_path);
                }
            }
        }

        return stem_match;
    };

    std::vector<std::string> raw_candidates;
    raw_candidates.reserve(12);
    if (texture) {
        raw_candidates.push_back(to_raw_string(texture->filename));
        raw_candidates.push_back(to_raw_string(texture->relative_filename));
        raw_candidates.push_back(to_raw_string(texture->absolute_filename));
        for (size_t i = 0; i < texture->file_textures.count; ++i) {
            const ufbx_texture *file_texture = texture->file_textures.data[i];
            if (!file_texture) {
                continue;
            }
            raw_candidates.push_back(to_raw_string(file_texture->filename));
            raw_candidates.push_back(to_raw_string(file_texture->relative_filename));
            raw_candidates.push_back(to_raw_string(file_texture->absolute_filename));
        }
    }

    for (const std::string &raw : raw_candidates) {
        if (std::optional<fs::path> resolved = find_existing_candidate(raw)) {
            return *resolved;
        }
    }

    for (const std::string &raw : raw_candidates) {
        if (raw.empty()) {
            continue;
        }
        const std::string expanded_text = expand_texture_tokens(raw);
        if (std::optional<fs::path> resolved = find_texture_by_stem(fs::path(expanded_text))) {
            return *resolved;
        }
    }

    if (!material_debug_name.empty()) {
        if (std::optional<fs::path> resolved = find_texture_by_stem(fs::path(material_debug_name))) {
            return *resolved;
        }
    }
    if (!material_safe_name.empty()) {
        if (std::optional<fs::path> resolved = find_texture_by_stem(fs::path(material_safe_name))) {
            return *resolved;
        }
    }

    return {};
}

std::optional<fs::path> find_texture_by_stem_in_tree(const fs::path &root, const fs::path &requested_name)
{
    const std::string wanted_filename = to_lower_ascii(requested_name.filename().string());
    const std::string wanted_stem = to_lower_ascii(requested_name.stem().string());
    if (wanted_filename.empty() && wanted_stem.empty()) {
        return std::nullopt;
    }

    std::error_code ec;
    if (!fs::exists(root, ec) || ec) {
        return std::nullopt;
    }

    std::optional<fs::path> stem_match;
    for (const fs::directory_entry &entry :
         fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            continue;
        }
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }

        const fs::path entry_path = entry.path();
        const std::string entry_filename = to_lower_ascii(entry_path.filename().string());
        if (!wanted_filename.empty() && entry_filename == wanted_filename) {
            return fs::absolute(entry_path);
        }

        if (!stem_match.has_value()) {
            const std::string entry_stem = to_lower_ascii(entry_path.stem().string());
            if (!wanted_stem.empty() && entry_stem == wanted_stem) {
                stem_match = fs::absolute(entry_path);
            }
        }
    }

    return stem_match;
}

std::unordered_map<std::string, std::string> load_material_texture_overrides(const fs::path &input_dir)
{
    std::unordered_map<std::string, std::string> overrides;
    const fs::path override_path = input_dir / "material_texture_overrides.txt";

    std::ifstream in(override_path);
    if (!in) {
        return overrides;
    }

    std::string line;
    size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        std::string parsed = trim_ascii(line);
        if (parsed.empty() || parsed[0] == '#') {
            continue;
        }

        const size_t equals = parsed.find('=');
        if (equals == std::string::npos) {
            std::cerr << "Warning: ignored invalid override line " << line_number << " in " << override_path << "\n";
            continue;
        }

        const std::string material_name = trim_ascii(std::string_view(parsed).substr(0, equals));
        const std::string texture_hint = trim_ascii(std::string_view(parsed).substr(equals + 1));
        if (material_name.empty() || texture_hint.empty()) {
            std::cerr << "Warning: ignored invalid override line " << line_number << " in " << override_path << "\n";
            continue;
        }

        overrides[material_name] = texture_hint;
    }

    if (!overrides.empty()) {
        std::cout << "Loaded material texture overrides: " << overrides.size() << " from " << override_path << "\n";
    }
    return overrides;
}

fs::path resolve_texture_override_hint(const std::string &hint, const fs::path &input_dir)
{
    const fs::path texture_root = input_dir.parent_path() / "textures";
    std::vector<fs::path> candidates;
    candidates.reserve(5);

    const fs::path raw_path = fs::path(hint);
    candidates.push_back(raw_path);
    candidates.push_back(input_dir / raw_path);
    candidates.push_back(texture_root / raw_path);
    candidates.push_back(texture_root / raw_path.filename());

    std::error_code ec;
    for (const fs::path &candidate : candidates) {
        const fs::path normalized = candidate.lexically_normal();
        ec.clear();
        if (!normalized.empty() && fs::exists(normalized, ec) && !ec) {
            return fs::absolute(normalized);
        }
    }

    if (std::optional<fs::path> by_stem = find_texture_by_stem_in_tree(texture_root, raw_path)) {
        return *by_stem;
    }
    return {};
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const fs::path input_path = fs::path(argv[1]);
    const fs::path output_path = fs::path(argv[2]);
    uint64_t triangle_step = 1;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--triangle-step") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --triangle-step\n";
                return 1;
            }

            uint64_t parsed_step = 0;
            if (!parse_positive_u64(argv[i + 1], parsed_step)) {
                std::cerr << "Invalid --triangle-step value: " << argv[i + 1] << "\n";
                return 1;
            }

            triangle_step = parsed_step;
            ++i;
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        print_usage(argv[0]);
        return 1;
    }

    const fs::path output_dir = output_path.parent_path();
    const fs::path mtl_path = output_path.parent_path() / output_path.stem().concat(".mtl");
    const fs::path output_texture_dir = output_dir / "textures";
    std::error_code fs_error;
    fs::create_directories(output_dir, fs_error);
    if (fs_error) {
        std::cerr << "Failed to create output directory: " << output_dir << "\n";
        return 1;
    }
    fs::create_directories(output_texture_dir, fs_error);
    if (fs_error) {
        std::cerr << "Failed to create texture output directory: " << output_texture_dir << "\n";
        return 1;
    }

    ufbx_load_opts opts{};
    opts.load_external_files = false;
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;

    ufbx_error error{};
    ufbx_scene *scene = ufbx_load_file(input_path.string().c_str(), &opts, &error);
    if (!scene) {
        char buffer[1024];
        ufbx_format_error(buffer, sizeof(buffer), &error);
        std::cerr << "Failed to load FBX: " << input_path << "\n";
        std::cerr << buffer << "\n";
        return 1;
    }

    std::ofstream obj_out(output_path);
    if (!obj_out) {
        std::cerr << "Failed to open output OBJ: " << output_path << "\n";
        ufbx_free_scene(scene);
        return 1;
    }

    std::ofstream mtl_out(mtl_path);
    if (!mtl_out) {
        std::cerr << "Failed to open output MTL: " << mtl_path << "\n";
        ufbx_free_scene(scene);
        return 1;
    }

    obj_out.imbue(std::locale::classic());
    mtl_out.imbue(std::locale::classic());
    obj_out << std::fixed << std::setprecision(6);
    mtl_out << std::fixed << std::setprecision(6);

    obj_out << "# Generated by DynLex fbx_to_obj\n";
    obj_out << "# Source: " << input_path.string() << "\n";
    obj_out << "mtllib " << mtl_path.string() << "\n";

    std::vector<MaterialOutput> materials;
    materials.reserve(scene->materials.count + 1);

    const std::unordered_map<std::string, std::string> material_texture_overrides =
        load_material_texture_overrides(input_path.parent_path());

    std::unordered_map<const ufbx_material *, size_t> material_index;
    material_index.reserve(scene->materials.count + 1);

    auto ensure_material = [&](const ufbx_material *material) -> size_t {
        auto it = material_index.find(material);
        if (it != material_index.end()) {
            return it->second;
        }

        MaterialOutput out;
        out.debug_name = material ? to_string(material->name) : "default";
        std::string base = sanitize_name(out.debug_name);
        out.safe_name = base;
        size_t suffix = 1;
        while (true) {
            bool unique = true;
            for (const MaterialOutput &existing : materials) {
                if (existing.safe_name == out.safe_name) {
                    unique = false;
                    break;
                }
            }
            if (unique) {
                break;
            }
            out.safe_name = base + "_" + std::to_string(suffix++);
        }

        pick_diffuse_color(material, out.kd_r, out.kd_g, out.kd_b);

        const ufbx_texture *texture = pick_color_texture(material);
        fs::path source_path;

        const auto override_it = material_texture_overrides.find(out.debug_name);
        if (override_it != material_texture_overrides.end()) {
            source_path = resolve_texture_override_hint(override_it->second, input_path.parent_path());
            if (source_path.empty()) {
                std::cerr << "Warning: texture override for material '" << out.debug_name
                          << "' did not resolve: " << override_it->second << "\n";
            }
        }

        if (source_path.empty()) {
            source_path = pick_texture_source_path(texture, input_path.parent_path(), out.debug_name, out.safe_name);
        }
        if (source_path.empty()) {
            static constexpr std::string_view kTextureExtensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
            const fs::path texture_root = input_path.parent_path().parent_path() / "textures";
            for (std::string_view ext : kTextureExtensions) {
                const fs::path candidate = texture_root / (out.safe_name + std::string(ext));
                std::error_code ec;
                if (fs::exists(candidate, ec) && !ec) {
                    source_path = fs::absolute(candidate);
                    break;
                }
            }

            if (source_path.empty()) {
                const fs::path color_wheel = texture_root / "lowpoly_tex-2.png";
                std::error_code ec;
                if (fs::exists(color_wheel, ec) && !ec) {
                    source_path = fs::absolute(color_wheel);
                }
            }

        }
        out.texture_source = source_path.string();

        if (texture || !source_path.empty()) {
            int width = 0;
            int height = 0;
            std::vector<uint8_t> rgb;
            if (load_texture_rgb(texture, source_path, width, height, rgb)) {
                fs::path ppm_path = output_texture_dir / (out.safe_name + ".ppm");
                if (write_ppm(ppm_path, width, height, rgb)) {
                    out.has_texture = true;
                    out.texture_ppm_abs = fs::absolute(ppm_path).string();
                } else {
                    std::cerr << "Warning: could not write texture PPM: " << ppm_path << "\n";
                }
            } else {
                std::cerr << "Warning: could not decode texture for material '" << out.debug_name << "'";
                if (!source_path.empty()) {
                    std::cerr << " from " << source_path;
                }
                std::cerr << "\n";
            }
        }

        if (!out.has_texture) {
            out.kd_r = 0.8;
            out.kd_g = 0.8;
            out.kd_b = 0.8;
        }

        const size_t index = materials.size();
        materials.push_back(std::move(out));
        material_index[material] = index;
        return index;
    };

    const size_t default_material_index = ensure_material(nullptr);

    uint64_t next_vertex_index = 1;
    uint64_t next_uv_index = 1;
    uint64_t emitted_triangles = 0;
    uint64_t written_vertices = 0;
    uint64_t written_colored_vertices = 0;
    uint64_t written_uvs = 0;
    uint64_t total_source_triangles = 0;
    Bounds bounds;

    int active_material_index = -1;

    for (size_t mesh_ix = 0; mesh_ix < scene->meshes.count; ++mesh_ix) {
        const ufbx_mesh *mesh = scene->meshes.data[mesh_ix];
        if (!mesh->vertex_position.exists) {
            continue;
        }

        const bool has_uv = mesh->vertex_uv.exists;
        const bool has_vertex_color = mesh->vertex_color.exists;
        const size_t tri_index_capacity = std::max<size_t>(mesh->max_face_triangles * 3, 3);
        std::vector<uint32_t> tri_indices(tri_index_capacity);

        const size_t instance_count = mesh->instances.count > 0 ? mesh->instances.count : 1;
        for (size_t inst_ix = 0; inst_ix < instance_count; ++inst_ix) {
            const ufbx_node *node = mesh->instances.count > 0 ? mesh->instances.data[inst_ix] : nullptr;
            const std::string mesh_name = to_string(mesh->name);
            const std::string node_name = node ? to_string(node->name) : std::string("local");
            obj_out << "o " << sanitize_name(mesh_name) << "_" << sanitize_name(node_name) << "\n";

            for (size_t face_ix = 0; face_ix < mesh->faces.count; ++face_ix) {
                const ufbx_face face = mesh->faces.data[face_ix];
                const uint32_t tri_count = ufbx_triangulate_face(tri_indices.data(), tri_indices.size(), mesh, face);
                total_source_triangles += tri_count;

                uint32_t face_material_slot = 0;
                if (face_ix < mesh->face_material.count) {
                    face_material_slot = mesh->face_material.data[face_ix];
                }

                const ufbx_material *face_material = nullptr;
                if (node && face_material_slot < node->materials.count) {
                    face_material = node->materials.data[face_material_slot];
                }
                if (!face_material && face_material_slot < mesh->materials.count) {
                    face_material = mesh->materials.data[face_material_slot];
                }

                size_t material_idx = default_material_index;
                if (face_material) {
                    material_idx = ensure_material(face_material);
                }

                for (uint32_t tri_ix = 0; tri_ix < tri_count; ++tri_ix) {
                    const uint64_t triangle_global_index = total_source_triangles - tri_count + tri_ix;
                    if (triangle_global_index % triangle_step != 0) {
                        continue;
                    }

                    if (active_material_index != static_cast<int>(material_idx)) {
                        obj_out << "usemtl " << materials[material_idx].safe_name << "\n";
                        active_material_index = static_cast<int>(material_idx);
                    }

                    for (uint32_t corner = 0; corner < 3; ++corner) {
                        const uint32_t index = tri_indices[tri_ix * 3 + corner];

                        ufbx_vec3 position = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                        if (node) {
                            position = ufbx_transform_position(&node->geometry_to_world, position);
                        }
                        if (has_vertex_color) {
                            const ufbx_vec4 color = ufbx_get_vertex_vec4(&mesh->vertex_color, index);
                            const double color_r = std::clamp(static_cast<double>(color.x), 0.0, 1.0);
                            const double color_g = std::clamp(static_cast<double>(color.y), 0.0, 1.0);
                            const double color_b = std::clamp(static_cast<double>(color.z), 0.0, 1.0);
                            obj_out << "v " << position.x << " " << position.y << " " << position.z << " " << color_r
                                    << " " << color_g << " " << color_b << "\n";
                            written_colored_vertices += 1;
                        } else {
                            obj_out << "v " << position.x << " " << position.y << " " << position.z << "\n";
                        }
                        bounds.include(position);

                        ufbx_vec2 uv{};
                        if (has_uv) {
                            uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                        }
                        obj_out << "vt " << uv.x << " " << uv.y << "\n";
                    }

                    obj_out << "f " << next_vertex_index << "/" << next_uv_index << " " << (next_vertex_index + 1) << "/"
                            << (next_uv_index + 1) << " " << (next_vertex_index + 2) << "/" << (next_uv_index + 2)
                            << "\n";

                    next_vertex_index += 3;
                    next_uv_index += 3;
                    written_vertices += 3;
                    written_uvs += 3;
                    emitted_triangles += 1;
                }
            }
        }
    }

    for (const MaterialOutput &material : materials) {
        mtl_out << "newmtl " << material.safe_name << "\n";
        mtl_out << "Kd " << material.kd_r << " " << material.kd_g << " " << material.kd_b << "\n";
        mtl_out << "Ka 0.000000 0.000000 0.000000\n";
        mtl_out << "Ks 0.000000 0.000000 0.000000\n";
        mtl_out << "d 1.000000\n";
        mtl_out << "illum 1\n";
        if (material.has_texture) {
            mtl_out << "map_Kd " << material.texture_ppm_abs << "\n";
        }
        mtl_out << "\n";
    }

    obj_out.flush();
    mtl_out.flush();
    if (!obj_out) {
        std::cerr << "Failed while writing OBJ: " << output_path << "\n";
        ufbx_free_scene(scene);
        return 1;
    }
    if (!mtl_out) {
        std::cerr << "Failed while writing MTL: " << mtl_path << "\n";
        ufbx_free_scene(scene);
        return 1;
    }

    std::cout << "Wrote OBJ: " << output_path << "\n";
    std::cout << "Wrote MTL: " << mtl_path << "\n";
    std::cout << "Material count: " << materials.size() << "\n";

    size_t textured_materials = 0;
    for (const MaterialOutput &material : materials) {
        if (material.has_texture) {
            textured_materials += 1;
        }
    }
    std::cout << "Textured materials: " << textured_materials << "\n";

    std::cout << "Source triangles: " << total_source_triangles << "\n";
    std::cout << "Emitted triangles: " << emitted_triangles << " (step=" << triangle_step << ")\n";
    std::cout << "Vertices written: " << written_vertices << "\n";
    std::cout << "Vertices with color: " << written_colored_vertices << "\n";
    std::cout << "UVs written: " << written_uvs << "\n";
    if (bounds.valid()) {
        std::cout << "Bounds min: (" << bounds.min_x << ", " << bounds.min_y << ", " << bounds.min_z << ")\n";
        std::cout << "Bounds max: (" << bounds.max_x << ", " << bounds.max_y << ", " << bounds.max_z << ")\n";
    }

    ufbx_free_scene(scene);
    return 0;
}
