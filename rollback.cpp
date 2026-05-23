// themis_rollback.cpp
// C++17 rollback engine for Themis.
//
// Behavior:
//   - Looks for themis_history.jsonl in the current directory by default.
//   - Reads move history lines produced by Themis.
//   - Restores moved files by moving destination -> source.
//   - Processes entries in reverse order, so the latest moves are undone first.
//   - Default mode is dry-run for safety. Use --apply to actually move files.
//
// Build:
//   g++ themis_rollback.cpp -o themis-rollback -std=c++17 -O2
//
// Usage:
//   ./themis-rollback
//   ./themis-rollback --apply
//   ./themis-rollback --history ./themis_history.jsonl --apply
//   ./themis-rollback --last 20 --apply

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Options {
    fs::path history = "themis_history.jsonl";
    bool apply = false;
    bool keepEmptyDirs = false;
    bool overwrite = false;
    int last = -1;
};

struct HistoryEntry {
    std::string source;
    std::string destination;
    std::string appliedAt;
    std::string raw;
};

static void print_help() {
    std::cout <<
        "Themis Rollback Engine\n"
        "Undo file moves recorded in themis_history.jsonl.\n\n"
        "Default behavior is dry-run. Use --apply to really move files back.\n\n"
        "Usage:\n"
        "  themis-rollback [options]\n\n"
        "Options:\n"
        "  --history FILE       History file path. Default: ./themis_history.jsonl\n"
        "  --apply              Actually perform rollback moves. Without this, dry-run only.\n"
        "  --last N             Roll back only the last N history entries.\n"
        "  --overwrite          Overwrite source path if it already exists. Disabled by default.\n"
        "  --keep-empty-dirs    Do not remove empty destination folders after rollback.\n"
        "  --help               Show this help message.\n";
}

static std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default:
                    // Minimal parser: keep unknown escapes as-is.
                    out.push_back(n);
                    break;
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static bool extract_json_string(const std::string& line, const std::string& key, std::string& value) {
    // Minimal JSONL string extractor for fields like "source":"...".
    // It supports escaped quotes/backslashes well enough for Themis history lines.
    std::string pattern = "\"" + key + "\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"";
    std::regex re(pattern);
    std::smatch m;
    if (!std::regex_search(line, m, re)) return false;
    value = json_unescape(m[1].str());
    return true;
}

static Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto need = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + name);
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            print_help();
            std::exit(0);
        } else if (arg == "--history") {
            opt.history = need(arg);
        } else if (arg == "--apply") {
            opt.apply = true;
        } else if (arg == "--last") {
            opt.last = std::stoi(need(arg));
        } else if (arg == "--overwrite") {
            opt.overwrite = true;
        } else if (arg == "--keep-empty-dirs") {
            opt.keepEmptyDirs = true;
        } else {
            throw std::runtime_error("Unknown option: " + arg);
        }
    }
    return opt;
}

static std::vector<HistoryEntry> read_history(const fs::path& historyPath) {
    std::ifstream f(historyPath);
    if (!f) throw std::runtime_error("Cannot open history file: " + historyPath.string());

    std::vector<HistoryEntry> entries;
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (line.empty()) continue;

        HistoryEntry e;
        e.raw = line;
        bool hasSource = extract_json_string(line, "source", e.source);
        bool hasDestination = extract_json_string(line, "destination", e.destination);
        extract_json_string(line, "applied_at", e.appliedAt);

        if (!hasSource || !hasDestination) {
            std::cerr << "Warning: ignored invalid history line " << lineNo << "\n";
            continue;
        }
        entries.push_back(std::move(e));
    }
    return entries;
}

static void remove_empty_parents(fs::path dir) {
    std::error_code ec;
    while (!dir.empty() && fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
        if (!fs::is_empty(dir, ec)) break;
        fs::remove(dir, ec);
        if (ec) break;
        dir = dir.parent_path();
    }
}

static bool rollback_one(const HistoryEntry& e, const Options& opt, int index, int total) {
    fs::path from = e.destination;
    fs::path to = e.source;

    std::cout << '[' << index << '/' << total << "] ";
    std::cout << from.string() << " -> " << to.string();
    if (!e.appliedAt.empty()) std::cout << "  (" << e.appliedAt << ")";
    std::cout << "\n";

    std::error_code ec;
    if (!fs::exists(from, ec)) {
        std::cout << "  SKIP: destination file does not exist anymore.\n";
        return false;
    }

    if (fs::exists(to, ec) && !opt.overwrite) {
        std::cout << "  SKIP: original source path already exists. Use --overwrite to replace it.\n";
        return false;
    }

    if (!opt.apply) {
        std::cout << "  DRY-RUN: no file moved. Use --apply to perform rollback.\n";
        return true;
    }

    fs::create_directories(to.parent_path(), ec);
    if (ec) {
        std::cout << "  ERROR: cannot create source parent folder: " << ec.message() << "\n";
        return false;
    }

    if (fs::exists(to, ec) && opt.overwrite) {
        fs::remove(to, ec);
        if (ec) {
            std::cout << "  ERROR: cannot overwrite existing source path: " << ec.message() << "\n";
            return false;
        }
    }

    fs::rename(from, to, ec);
    if (ec) {
        // Cross-device fallback: copy then remove.
        ec.clear();
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cout << "  ERROR: rollback copy failed: " << ec.message() << "\n";
            return false;
        }
        fs::remove(from, ec);
        if (ec) {
            std::cout << "  WARNING: copied back but could not remove moved file: " << ec.message() << "\n";
            return true;
        }
    }

    if (!opt.keepEmptyDirs) {
        remove_empty_parents(from.parent_path());
    }

    std::cout << "  OK: restored.\n";
    return true;
}

int main(int argc, char** argv) {
    try {
        Options opt = parse_args(argc, argv);

        if (!fs::exists(opt.history)) {
            std::cerr << "Error: history file not found in current directory: " << opt.history.string() << "\n";
            std::cerr << "Tip: run this program from the folder containing themis_history.jsonl, or use --history PATH.\n";
            return 1;
        }

        std::vector<HistoryEntry> entries = read_history(opt.history);
        if (entries.empty()) {
            std::cout << "No valid move entries found in " << opt.history.string() << "\n";
            return 0;
        }

        if (opt.last > 0 && static_cast<size_t>(opt.last) < entries.size()) {
            entries.erase(entries.begin(), entries.end() - opt.last);
        }

        std::reverse(entries.begin(), entries.end());

        std::cout << "Themis Rollback Engine\n";
        std::cout << "History: " << opt.history.string() << "\n";
        std::cout << "Entries to rollback: " << entries.size() << "\n";
        std::cout << "Mode: " << (opt.apply ? "APPLY" : "DRY-RUN") << "\n";
        if (!opt.apply) std::cout << "No files will be moved unless you run again with --apply.\n";
        std::cout << "\n";

        int ok = 0;
        int total = static_cast<int>(entries.size());
        for (int i = 0; i < total; ++i) {
            if (rollback_one(entries[i], opt, i + 1, total)) ++ok;
        }

        std::cout << "\nSummary: " << ok << '/' << total << " entries are rollback-ready or restored.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
