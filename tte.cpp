// themis_tree_exporter.cpp
// C++17 local tree scanner for Themis.
//
// It recursively scans a local folder and writes:
//   1. themis_tree_inventory.csv  -> detailed file tree inventory
//   2. themis_seed_plan.csv       -> Themis-compatible CSV columns, marked selected=false
//
// Build:
//   g++ themis_tree_exporter.cpp -o themis-tree-exporter -std=c++17 -O2
//
// Run:
//   ./themis-tree-exporter
//   ./themis-tree-exporter --root /path/to/folder --output inventory.csv --seed-plan seed_plan.csv

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

struct Options {
    fs::path root = fs::current_path();
    fs::path output = "themis_tree_inventory.csv";
    fs::path seedPlan = "themis_seed_plan.csv";
    bool includeHidden = false;
    bool followSymlinks = false;
    bool writeSeedPlan = true;
    int maxDepth = -1;
};

struct Entry {
    std::string type;
    fs::path path;
    fs::path parent;
    std::string name;
    std::string extension;
    std::uintmax_t size = 0;
    int depth = 0;
    std::string modified;
    bool hidden = false;
    bool readable = true;
    std::string error;
};

static void print_help() {
    std::cout <<
        "Themis Tree Exporter\n"
        "Recursively scans a local folder and produces CSV files for Themis workflows.\n\n"
        "Usage:\n"
        "  themis-tree-exporter [options]\n\n"
        "Options:\n"
        "  --root PATH           Root folder to scan. Default: current directory.\n"
        "  --output FILE         Inventory CSV output. Default: themis_tree_inventory.csv.\n"
        "  --seed-plan FILE      Themis-compatible seed plan CSV. Default: themis_seed_plan.csv.\n"
        "  --no-seed-plan        Do not write the Themis-compatible seed plan CSV.\n"
        "  --include-hidden      Include hidden files and folders.\n"
        "  --follow-symlinks     Follow directory symlinks. Disabled by default to avoid loops.\n"
        "  --max-depth N         Maximum recursion depth. Default: unlimited.\n"
        "  --help                Show this help message.\n";
}

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

static std::string csv_escape(const std::string& value) {
    bool mustQuote = false;
    for (char c : value) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            mustQuote = true;
            break;
        }
    }
    if (!mustQuote) return value;

    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

static std::string bool_str(bool v) {
    return v ? "true" : "false";
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

static int compute_depth(const fs::path& p, const fs::path& root) {
    std::error_code ec;
    fs::path rel = fs::relative(p, root, ec);
    if (ec || rel.empty() || rel == ".") return 0;

    int d = 0;
    for (const auto& ignored : rel) {
        (void)ignored;
        ++d;
    }
    return std::max(0, d - 1);
}

static std::string file_time_to_string(const fs::file_time_type& ft) {
    // Portable enough conversion for C++17 implementations.
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    std::time_t t = std::chrono::system_clock::to_time_t(sctp);

    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

static std::string safe_category_from_parent(const fs::path& parent) {
    std::string s = parent.filename().string();
    if (s.empty() || s == "." || s == "..") return "unsorted";

    for (char& c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_' || c == '-')) c = '_';
    }
    return s.empty() ? "unsorted" : s;
}

static Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto require_value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for " + name);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            print_help();
            std::exit(0);
        } else if (arg == "--root") {
            opt.root = require_value(arg);
        } else if (arg == "--output") {
            opt.output = require_value(arg);
        } else if (arg == "--seed-plan") {
            opt.seedPlan = require_value(arg);
            opt.writeSeedPlan = true;
        } else if (arg == "--no-seed-plan") {
            opt.writeSeedPlan = false;
        } else if (arg == "--include-hidden") {
            opt.includeHidden = true;
        } else if (arg == "--follow-symlinks") {
            opt.followSymlinks = true;
        } else if (arg == "--max-depth") {
            opt.maxDepth = std::stoi(require_value(arg));
        } else {
            throw std::runtime_error("Unknown option: " + arg);
        }
    }
    return opt;
}

static std::vector<Entry> scan_tree(const Options& opt) {
    std::vector<Entry> entries;
    std::error_code ec;

    fs::path root = fs::absolute(opt.root, ec);
    if (ec) throw std::runtime_error("Cannot resolve root path: " + opt.root.string());
    if (!fs::exists(root, ec)) throw std::runtime_error("Root does not exist: " + root.string());
    if (!fs::is_directory(root, ec)) throw std::runtime_error("Root is not a directory: " + root.string());

    fs::directory_options dirOptions = fs::directory_options::skip_permission_denied;
    if (opt.followSymlinks) {
        dirOptions |= fs::directory_options::follow_directory_symlink;
    }

    fs::recursive_directory_iterator it(root, dirOptions, ec);
    fs::recursive_directory_iterator end;

    while (it != end) {
        const fs::directory_entry de = *it;
        fs::path p = de.path();
        Entry e;
        e.path = p;
        e.parent = p.parent_path();
        e.name = p.filename().string();
        e.extension = p.has_extension() ? p.extension().string() : "";
        e.depth = compute_depth(p, root);
        e.hidden = is_hidden_path(p, root);

        if (!opt.includeHidden && e.hidden) {
            if (de.is_directory(ec)) it.disable_recursion_pending();
            it.increment(ec);
            continue;
        }

        if (opt.maxDepth >= 0 && e.depth > opt.maxDepth) {
            if (de.is_directory(ec)) it.disable_recursion_pending();
            it.increment(ec);
            continue;
        }

        ec.clear();
        if (de.is_directory(ec)) e.type = "directory";
        else if (de.is_regular_file(ec)) e.type = "file";
        else if (de.is_symlink(ec)) e.type = "symlink";
        else e.type = "other";

        ec.clear();
        if (e.type == "file") {
            e.size = de.file_size(ec);
            if (ec) {
                e.size = 0;
                e.readable = false;
                e.error = ec.message();
            }
        }

        ec.clear();
        auto ft = de.last_write_time(ec);
        if (!ec) e.modified = file_time_to_string(ft);
        else e.modified = "";

        entries.push_back(std::move(e));

        it.increment(ec);
        if (ec) {
            // Keep scanning if possible. Permission errors are expected on some systems.
            ec.clear();
        }
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.path.string() < b.path.string();
    });

    return entries;
}

static void write_inventory_csv(const fs::path& out, const std::vector<Entry>& entries) {
    std::ofstream f(out);
    if (!f) throw std::runtime_error("Cannot open output file: " + out.string());

    f << "type,path,parent,name,extension,size_bytes,depth,last_write_time,hidden,readable,error\n";
    for (const Entry& e : entries) {
        f
            << csv_escape(e.type) << ','
            << csv_escape(e.path.string()) << ','
            << csv_escape(e.parent.string()) << ','
            << csv_escape(e.name) << ','
            << csv_escape(e.extension) << ','
            << e.size << ','
            << e.depth << ','
            << csv_escape(e.modified) << ','
            << bool_str(e.hidden) << ','
            << bool_str(e.readable) << ','
            << csv_escape(e.error) << '\n';
    }
}

static void write_themis_seed_plan_csv(const fs::path& out, const std::vector<Entry>& entries) {
    std::ofstream f(out);
    if (!f) throw std::runtime_error("Cannot open seed plan file: " + out.string());

    // Current Themis CSV fields:
    // selected,source,destination,topic,topic_label,confidence,reason,model,category,bayes_label,bayes_confidence
    // selected=false prevents accidental moves if this file is applied by mistake.
    f << "selected,source,destination,topic,topic_label,confidence,reason,model,category,bayes_label,bayes_confidence\n";

    for (const Entry& e : entries) {
        if (e.type != "file") continue;
        std::string category = safe_category_from_parent(e.parent);
        std::string reason = "Imported from C++ local tree inventory. Review category and destination before applying.";

        f
            << "false" << ','
            << csv_escape(e.path.string()) << ','
            << csv_escape("") << ','
            << "0" << ','
            << csv_escape("cpp_inventory") << ','
            << "0" << ','
            << csv_escape(reason) << ','
            << csv_escape("cpp_inventory") << ','
            << csv_escape(category) << ','
            << csv_escape("") << ','
            << "0" << '\n';
    }
}

int main(int argc, char** argv) {
    try {
        Options opt = parse_args(argc, argv);
        opt.root = fs::absolute(opt.root);

        std::cout << "Themis Tree Exporter\n";
        std::cout << "Root: " << opt.root.string() << "\n";
        std::cout << "Scanning...\n";

        std::vector<Entry> entries = scan_tree(opt);

        write_inventory_csv(opt.output, entries);
        std::cout << "Inventory written: " << opt.output.string() << "\n";

        if (opt.writeSeedPlan) {
            write_themis_seed_plan_csv(opt.seedPlan, entries);
            std::cout << "Themis seed plan written: " << opt.seedPlan.string() << "\n";
            std::cout << "Note: seed plan rows are selected=false for safety. Review it before use.\n";
        }

        std::size_t files = 0, dirs = 0, others = 0;
        std::uintmax_t totalSize = 0;
        for (const Entry& e : entries) {
            if (e.type == "file") {
                ++files;
                totalSize += e.size;
            } else if (e.type == "directory") {
                ++dirs;
            } else {
                ++others;
            }
        }

        std::cout << "Done.\n";
        std::cout << "Directories: " << dirs << "\n";
        std::cout << "Files: " << files << "\n";
        std::cout << "Other entries: " << others << "\n";
        std::cout << "Total file size: " << totalSize << " bytes\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
