// clean_duplicates.cpp
// C++17 utility for Themis: empty folder scanner + duplicate detector in one program.
//
// It is safe by default:
//   - empty folder cleaning is dry-run unless --apply-empty-clean is passed
//   - duplicate detection never deletes anything
//
// Build:
//   g++ clean_duplicates.cpp -o clean-duplicates -std=c++17 -O2
//
// Usage:
//   ./clean-duplicates --root ./Downloads
//   ./clean-duplicates --root ./Downloads --empty-report empty.csv --duplicates-report duplicates.csv
//   ./clean-duplicates --root ./Downloads --apply-empty-clean
//   ./clean-duplicates --root ./Downloads --hash-full

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

struct Options {
    fs::path root = fs::current_path();
    fs::path emptyReport = "empty_folders.csv";
    fs::path duplicatesReport = "duplicates.csv";
    bool includeHidden = false;
    bool followSymlinks = false;
    bool applyEmptyClean = false;
    bool hashFull = false;
    std::uintmax_t partialBytes = 1024 * 1024;
};

struct FileInfo {
    fs::path path;
    std::uintmax_t size = 0;
    std::string partialHash;
    std::string fullHash;
};

static void print_help() {
    std::cout <<
        "Themis Clean + Duplicates\n"
        "Scan empty folders and detect duplicate files. Safe by default.\n\n"
        "Usage:\n"
        "  clean-duplicates [options]\n\n"
        "Options:\n"
        "  --root PATH              Root folder to scan. Default: current directory.\n"
        "  --empty-report FILE      CSV report for empty folders. Default: empty_folders.csv.\n"
        "  --duplicates-report FILE CSV report for duplicate candidates. Default: duplicates.csv.\n"
        "  --include-hidden         Include hidden files and folders.\n"
        "  --follow-symlinks        Follow directory symlinks. Disabled by default.\n"
        "  --apply-empty-clean      Actually remove empty folders. Default is dry-run.\n"
        "  --hash-full              Compute full-file hashes for candidate duplicates. Slower but stronger.\n"
        "  --partial-bytes N        Bytes read for partial hash. Default: 1048576.\n"
        "  --help                   Show this help.\n";
}

static std::string csv_escape(const std::string& value) {
    bool quote = false;
    for (char c : value) if (c == ',' || c == '"' || c == '\n' || c == '\r') quote = true;
    if (!quote) return value;
    std::string out = "\"";
    for (char c : value) out += (c == '"') ? "\"\"" : std::string(1, c);
    out += "\"";
    return out;
}

static bool is_hidden_path(const fs::path& p, const fs::path& root) {
    std::error_code ec;
    fs::path rel = fs::relative(p, root, ec);
    if (ec) rel = p.filename();
    for (const auto& part : rel) {
        std::string s = part.string();
        if (!s.empty() && s[0] == '.') return true;
    }
    return false;
}

static Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + name);
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { print_help(); std::exit(0); }
        else if (a == "--root") opt.root = need(a);
        else if (a == "--empty-report") opt.emptyReport = need(a);
        else if (a == "--duplicates-report") opt.duplicatesReport = need(a);
        else if (a == "--include-hidden") opt.includeHidden = true;
        else if (a == "--follow-symlinks") opt.followSymlinks = true;
        else if (a == "--apply-empty-clean") opt.applyEmptyClean = true;
        else if (a == "--hash-full") opt.hashFull = true;
        else if (a == "--partial-bytes") opt.partialBytes = static_cast<std::uintmax_t>(std::stoull(need(a)));
        else throw std::runtime_error("Unknown option: " + a);
    }
    opt.root = fs::absolute(opt.root);
    return opt;
}

// FNV-1a 64-bit. Not cryptographic, but good enough for local duplicate candidates.
static std::uint64_t fnv1a_update(std::uint64_t hash, const char* data, std::size_t n) {
    const std::uint64_t prime = 1099511628211ULL;
    for (std::size_t i = 0; i < n; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= prime;
    }
    return hash;
}

static std::string hex64(std::uint64_t value) {
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << value;
    return oss.str();
}

static std::string hash_file(const fs::path& path, std::uintmax_t limitBytes, bool full) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "READ_ERROR";
    std::uint64_t hash = 14695981039346656037ULL;
    std::array<char, 64 * 1024> buf{};
    std::uintmax_t readTotal = 0;
    while (f) {
        std::size_t want = buf.size();
        if (!full) {
            if (readTotal >= limitBytes) break;
            std::uintmax_t remaining = limitBytes - readTotal;
            if (remaining < want) want = static_cast<std::size_t>(remaining);
        }
        f.read(buf.data(), static_cast<std::streamsize>(want));
        std::streamsize got = f.gcount();
        if (got <= 0) break;
        hash = fnv1a_update(hash, buf.data(), static_cast<std::size_t>(got));
        readTotal += static_cast<std::uintmax_t>(got);
    }
    return hex64(hash);
}

static std::vector<fs::path> collect_directories(const Options& opt) {
    std::vector<fs::path> dirs;
    std::error_code ec;
    if (!fs::exists(opt.root, ec) || !fs::is_directory(opt.root, ec)) {
        throw std::runtime_error("Root does not exist or is not a directory: " + opt.root.string());
    }
    fs::directory_options flags = fs::directory_options::skip_permission_denied;
    if (opt.followSymlinks) flags |= fs::directory_options::follow_directory_symlink;
    fs::recursive_directory_iterator it(opt.root, flags, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        fs::path p = it->path();
        if (!opt.includeHidden && is_hidden_path(p, opt.root)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(ec)) dirs.push_back(p);
    }
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
        return a.string().size() > b.string().size(); // deepest first
    });
    return dirs;
}

static std::vector<fs::path> find_empty_dirs(const Options& opt) {
    auto dirs = collect_directories(opt);
    std::vector<fs::path> empty;
    std::error_code ec;
    for (const auto& d : dirs) {
        ec.clear();
        if (fs::exists(d, ec) && fs::is_directory(d, ec) && fs::is_empty(d, ec)) empty.push_back(d);
    }
    return empty;
}

static int remove_empty_dirs(const std::vector<fs::path>& dirs) {
    int removed = 0;
    std::error_code ec;
    for (const auto& d : dirs) {
        ec.clear();
        if (fs::exists(d, ec) && fs::is_directory(d, ec) && fs::is_empty(d, ec)) {
            fs::remove(d, ec);
            if (!ec) ++removed;
        }
    }
    return removed;
}

static void write_empty_report(const fs::path& out, const std::vector<fs::path>& dirs, bool applyMode) {
    std::ofstream f(out);
    if (!f) throw std::runtime_error("Cannot write empty folder report: " + out.string());
    f << "path,action\n";
    for (const auto& d : dirs) {
        f << csv_escape(d.string()) << ',' << (applyMode ? "remove_if_still_empty" : "dry_run_only") << '\n';
    }
}

static std::vector<FileInfo> collect_files(const Options& opt) {
    std::vector<FileInfo> files;
    std::error_code ec;
    fs::directory_options flags = fs::directory_options::skip_permission_denied;
    if (opt.followSymlinks) flags |= fs::directory_options::follow_directory_symlink;
    fs::recursive_directory_iterator it(opt.root, flags, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        fs::path p = it->path();
        if (!opt.includeHidden && is_hidden_path(p, opt.root)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        ec.clear();
        if (it->is_regular_file(ec)) {
            FileInfo info;
            info.path = p;
            info.size = it->file_size(ec);
            if (!ec) files.push_back(info);
        }
    }
    return files;
}

static std::vector<std::vector<FileInfo>> find_duplicates(const Options& opt) {
    auto files = collect_files(opt);
    std::unordered_map<std::uintmax_t, std::vector<FileInfo>> bySize;
    for (auto& f : files) bySize[f.size].push_back(f);

    std::vector<std::vector<FileInfo>> groups;

    for (auto& kv : bySize) {
        auto& sameSize = kv.second;
        if (sameSize.size() < 2) continue;

        std::unordered_map<std::string, std::vector<FileInfo>> byPartial;
        for (auto& f : sameSize) {
            f.partialHash = hash_file(f.path, opt.partialBytes, false);
            byPartial[f.partialHash].push_back(f);
        }

        for (auto& hp : byPartial) {
            auto candidates = hp.second;
            if (candidates.size() < 2) continue;

            if (opt.hashFull) {
                std::unordered_map<std::string, std::vector<FileInfo>> byFull;
                for (auto& f : candidates) {
                    f.fullHash = hash_file(f.path, 0, true);
                    byFull[f.fullHash].push_back(f);
                }
                for (auto& hf : byFull) if (hf.second.size() >= 2) groups.push_back(hf.second);
            } else {
                groups.push_back(candidates);
            }
        }
    }

    std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
        if (a.empty() || b.empty()) return a.size() > b.size();
        return a[0].size > b[0].size;
    });
    return groups;
}

static void write_duplicates_report(const fs::path& out, const std::vector<std::vector<FileInfo>>& groups, bool fullHash) {
    std::ofstream f(out);
    if (!f) throw std::runtime_error("Cannot write duplicates report: " + out.string());
    f << "group_id,size_bytes,partial_hash,full_hash,path\n";
    int groupId = 1;
    for (const auto& group : groups) {
        for (const auto& item : group) {
            f << groupId << ','
              << item.size << ','
              << csv_escape(item.partialHash) << ','
              << csv_escape(fullHash ? item.fullHash : "not_computed") << ','
              << csv_escape(item.path.string()) << '\n';
        }
        ++groupId;
    }
}

int main(int argc, char** argv) {
    try {
        Options opt = parse_args(argc, argv);

        std::cout << "Themis Clean + Duplicates\n";
        std::cout << "Root: " << opt.root.string() << "\n";
        std::cout << "Mode: " << (opt.applyEmptyClean ? "APPLY empty folder cleanup" : "DRY-RUN empty folder cleanup") << "\n";
        std::cout << "Duplicate hash mode: " << (opt.hashFull ? "partial + full hash" : "size + partial hash") << "\n\n";

        std::cout << "Scanning empty folders...\n";
        auto emptyDirs = find_empty_dirs(opt);
        write_empty_report(opt.emptyReport, emptyDirs, opt.applyEmptyClean);
        int removed = 0;
        if (opt.applyEmptyClean) removed = remove_empty_dirs(emptyDirs);

        std::cout << "Empty folders found: " << emptyDirs.size() << "\n";
        if (opt.applyEmptyClean) std::cout << "Empty folders removed: " << removed << "\n";
        std::cout << "Empty folder report: " << opt.emptyReport.string() << "\n\n";

        std::cout << "Scanning duplicate candidates...\n";
        auto duplicateGroups = find_duplicates(opt);
        write_duplicates_report(opt.duplicatesReport, duplicateGroups, opt.hashFull);

        std::size_t duplicateFiles = 0;
        std::uintmax_t duplicateWastedBytes = 0;
        for (const auto& group : duplicateGroups) {
            duplicateFiles += group.size();
            if (group.size() > 1) duplicateWastedBytes += (group.size() - 1) * group[0].size;
        }

        std::cout << "Duplicate groups: " << duplicateGroups.size() << "\n";
        std::cout << "Files in duplicate groups: " << duplicateFiles << "\n";
        std::cout << "Potential reclaimable bytes if keeping one per group: " << duplicateWastedBytes << "\n";
        std::cout << "Duplicates report: " << opt.duplicatesReport.string() << "\n\n";

        std::cout << "Done. No duplicate files were deleted.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
