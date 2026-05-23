// themis_safe_move_validator.cpp
// C++17 Safe Move Validator for Themis CSV plans.
//
// Purpose:
//   Validate a Themis move plan before applying it.
//   It does not move, copy, delete, or modify user files.
//
// Supported Themis CSV columns:
//   selected,source,destination,topic,topic_label,confidence,reason,model,category,bayes_label,bayes_confidence
//
// Build:
//   g++ themis_safe_move_validator.cpp -o themis-validator -std=c++17 -O2
//
// Usage:
//   ./themis-validator plan.csv
//   ./themis-validator plan.csv --report validation_report.csv
//   ./themis-validator plan.csv --include-unselected
//   ./themis-validator plan.csv --strict

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

struct Options {
    std::string planCsv;
    std::string reportCsv = "";
    bool includeUnselected = false;
    bool strict = false;
    bool quiet = false;
};

struct PlanRow {
    int rowNumber = 0;
    bool selected = true;
    std::string source;
    std::string destination;
    std::string model;
    std::string category;
};

struct ValidationIssue {
    int rowNumber = 0;
    std::string severity; // OK, WARNING, ERROR
    std::string code;
    std::string message;
    std::string source;
    std::string destination;
};

static void print_help() {
    std::cout <<
        "Themis Safe Move Validator\n"
        "Validate a Themis CSV move plan before applying it.\n\n"
        "Usage:\n"
        "  themis-validator PLAN.csv [options]\n\n"
        "Options:\n"
        "  --report FILE          Write detailed validation report as CSV.\n"
        "  --include-unselected   Validate rows even when selected=false.\n"
        "  --strict               Treat warnings as validation failure.\n"
        "  --quiet                Print only final summary.\n"
        "  --help                 Show this help message.\n\n"
        "Exit codes:\n"
        "  0 = no blocking errors\n"
        "  1 = errors found, or warnings found in --strict mode\n"
        "  2 = invalid arguments or unreadable plan\n";
}

static std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

static bool parse_bool(std::string s) {
    s = lower_ascii(trim(s));
    return s == "1" || s == "true" || s == "yes" || s == "y";
}

static std::string csv_escape(const std::string& value) {
    bool quote = false;
    for (char c : value) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            quote = true;
            break;
        }
    }
    if (!quote) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out += "\"";
    return out;
}

static std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> cells;
    std::string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur.push_back('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                cur.push_back(c);
            }
        } else {
            if (c == '"') inQuotes = true;
            else if (c == ',') {
                cells.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
    }
    cells.push_back(cur);
    return cells;
}

static Options parse_args(int argc, char** argv) {
    Options opt;
    if (argc < 2) {
        print_help();
        std::exit(2);
    }
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + name);
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            print_help();
            std::exit(0);
        } else if (a == "--report") {
            opt.reportCsv = need(a);
        } else if (a == "--include-unselected") {
            opt.includeUnselected = true;
        } else if (a == "--strict") {
            opt.strict = true;
        } else if (a == "--quiet") {
            opt.quiet = true;
        } else if (!a.empty() && a[0] == '-') {
            throw std::runtime_error("Unknown option: " + a);
        } else {
            if (!opt.planCsv.empty()) throw std::runtime_error("Only one plan CSV can be provided.");
            opt.planCsv = a;
        }
    }
    if (opt.planCsv.empty()) throw std::runtime_error("Missing plan CSV path.");
    return opt;
}

static std::vector<PlanRow> read_plan_csv(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open plan CSV: " + path);

    std::string header;
    if (!std::getline(f, header)) throw std::runtime_error("CSV is empty: " + path);

    auto cols = parse_csv_line(header);
    std::map<std::string, int> idx;
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) idx[trim(cols[i])] = i;

    auto require = [&](const std::string& name) {
        if (!idx.count(name)) throw std::runtime_error("Missing required CSV column: " + name);
    };
    require("selected");
    require("source");
    require("destination");

    auto get = [&](const std::vector<std::string>& cells, const std::string& name, const std::string& def = "") {
        auto it = idx.find(name);
        if (it == idx.end() || it->second >= static_cast<int>(cells.size())) return def;
        return cells[it->second];
    };

    std::vector<PlanRow> rows;
    std::string line;
    int rowNo = 1;
    while (std::getline(f, line)) {
        ++rowNo;
        if (trim(line).empty()) continue;
        auto cells = parse_csv_line(line);
        PlanRow r;
        r.rowNumber = rowNo;
        r.selected = parse_bool(get(cells, "selected", "true"));
        r.source = get(cells, "source");
        r.destination = get(cells, "destination");
        r.model = get(cells, "model", "");
        r.category = get(cells, "category", "");
        rows.push_back(r);
    }
    return rows;
}

static void add_issue(std::vector<ValidationIssue>& issues, const PlanRow& row,
                      const std::string& severity, const std::string& code, const std::string& message) {
    issues.push_back({row.rowNumber, severity, code, message, row.source, row.destination});
}

static bool is_probably_path_too_long(const fs::path& p) {
#if defined(_WIN32)
    return p.wstring().size() >= 240;
#else
    return p.string().size() >= 3900;
#endif
}

static bool has_invalid_windows_chars(const std::string& s) {
#if defined(_WIN32)
    static const std::string bad = "<>:\"|?*";
    for (char c : s) {
        if (bad.find(c) != std::string::npos) return true;
    }
#else
    (void)s;
#endif
    return false;
}

static bool can_create_parent(const fs::path& parent, std::string& error) {
    std::error_code ec;
    if (parent.empty()) {
        error = "destination parent is empty";
        return false;
    }
    if (fs::exists(parent, ec)) {
        if (!fs::is_directory(parent, ec)) {
            error = "destination parent exists but is not a directory";
            return false;
        }
        return true;
    }

    fs::path probe = parent;
    while (!probe.empty() && !fs::exists(probe, ec)) probe = probe.parent_path();
    if (probe.empty()) {
        error = "no existing ancestor for destination parent";
        return false;
    }
    if (!fs::is_directory(probe, ec)) {
        error = "nearest existing ancestor is not a directory";
        return false;
    }
    return true;
}

static std::vector<ValidationIssue> validate_plan(const std::vector<PlanRow>& rows, const Options& opt) {
    std::vector<ValidationIssue> issues;
    std::map<std::string, int> destinationCount;
    std::map<std::string, int> sourceCount;

    for (const auto& row : rows) {
        if (!row.selected && !opt.includeUnselected) continue;
        if (row.source.empty()) continue;
        sourceCount[fs::absolute(fs::path(row.source)).lexically_normal().string()]++;
        if (!row.destination.empty()) {
            destinationCount[fs::absolute(fs::path(row.destination)).lexically_normal().string()]++;
        }
    }

    for (const auto& row : rows) {
        if (!row.selected && !opt.includeUnselected) continue;

        if (!row.selected && opt.includeUnselected) {
            add_issue(issues, row, "WARNING", "UNSELECTED_ROW", "Row is selected=false but was validated because --include-unselected is enabled.");
        }

        if (trim(row.source).empty()) {
            add_issue(issues, row, "ERROR", "EMPTY_SOURCE", "Source path is empty.");
            continue;
        }
        if (trim(row.destination).empty()) {
            add_issue(issues, row, "ERROR", "EMPTY_DESTINATION", "Destination path is empty.");
            continue;
        }

        fs::path src(row.source);
        fs::path dst(row.destination);
        std::error_code ec;

        fs::path absSrc = fs::absolute(src, ec).lexically_normal();
        if (ec) {
            add_issue(issues, row, "ERROR", "SOURCE_ABSOLUTE_FAILED", "Cannot resolve source path: " + ec.message());
            continue;
        }
        ec.clear();
        fs::path absDst = fs::absolute(dst, ec).lexically_normal();
        if (ec) {
            add_issue(issues, row, "ERROR", "DESTINATION_ABSOLUTE_FAILED", "Cannot resolve destination path: " + ec.message());
            continue;
        }

        if (sourceCount[absSrc.string()] > 1) {
            add_issue(issues, row, "WARNING", "DUPLICATE_SOURCE", "The same source appears multiple times in the selected plan.");
        }
        if (destinationCount[absDst.string()] > 1) {
            add_issue(issues, row, "ERROR", "DUPLICATE_DESTINATION", "Multiple selected rows target the same destination path.");
        }

        ec.clear();
        if (!fs::exists(absSrc, ec)) {
            add_issue(issues, row, "ERROR", "SOURCE_MISSING", "Source file does not exist.");
            continue;
        }
        ec.clear();
        if (!fs::is_regular_file(absSrc, ec)) {
            add_issue(issues, row, "ERROR", "SOURCE_NOT_FILE", "Source exists but is not a regular file.");
        }

        if (absSrc == absDst) {
            add_issue(issues, row, "ERROR", "SAME_SOURCE_DESTINATION", "Source and destination are identical.");
        }

        ec.clear();
        if (fs::exists(absDst, ec)) {
            add_issue(issues, row, "WARNING", "DESTINATION_EXISTS", "Destination already exists. Themis may create a unique numbered path when applying the move.");
        }

        if (is_probably_path_too_long(absSrc)) {
            add_issue(issues, row, "WARNING", "SOURCE_PATH_LONG", "Source path is very long and may fail on some systems.");
        }
        if (is_probably_path_too_long(absDst)) {
            add_issue(issues, row, "WARNING", "DESTINATION_PATH_LONG", "Destination path is very long and may fail on some systems.");
        }

        if (has_invalid_windows_chars(dst.filename().string())) {
            add_issue(issues, row, "ERROR", "INVALID_DESTINATION_NAME", "Destination filename contains characters invalid on Windows.");
        }

        std::string parentError;
        if (!can_create_parent(absDst.parent_path(), parentError)) {
            add_issue(issues, row, "ERROR", "DESTINATION_PARENT_INVALID", parentError);
        }

        // Warn if destination is inside a hidden folder component.
        for (const auto& part : absDst) {
            std::string s = part.string();
            if (!s.empty() && s[0] == '.') {
                add_issue(issues, row, "WARNING", "DESTINATION_HIDDEN_FOLDER", "Destination path contains a hidden folder component.");
                break;
            }
        }
    }

    // Add OK rows when no issue exists for them.
    std::set<int> rowsWithIssues;
    for (const auto& issue : issues) rowsWithIssues.insert(issue.rowNumber);
    for (const auto& row : rows) {
        if (!row.selected && !opt.includeUnselected) continue;
        if (!rowsWithIssues.count(row.rowNumber)) {
            add_issue(issues, row, "OK", "VALID", "No blocking issue detected for this row.");
        }
    }

    std::sort(issues.begin(), issues.end(), [](const ValidationIssue& a, const ValidationIssue& b) {
        if (a.rowNumber != b.rowNumber) return a.rowNumber < b.rowNumber;
        auto rank = [](const std::string& s) { return s == "ERROR" ? 0 : (s == "WARNING" ? 1 : 2); };
        return rank(a.severity) < rank(b.severity);
    });

    return issues;
}

static void write_report_csv(const std::string& path, const std::vector<ValidationIssue>& issues) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot write report CSV: " + path);
    f << "row,severity,code,message,source,destination\n";
    for (const auto& i : issues) {
        f << i.rowNumber << ','
          << csv_escape(i.severity) << ','
          << csv_escape(i.code) << ','
          << csv_escape(i.message) << ','
          << csv_escape(i.source) << ','
          << csv_escape(i.destination) << '\n';
    }
}

int main(int argc, char** argv) {
    try {
        Options opt = parse_args(argc, argv);
        auto rows = read_plan_csv(opt.planCsv);
        auto issues = validate_plan(rows, opt);

        int selectedRows = 0;
        for (const auto& r : rows) if (r.selected || opt.includeUnselected) ++selectedRows;

        int ok = 0, warnings = 0, errors = 0;
        for (const auto& i : issues) {
            if (i.severity == "ERROR") ++errors;
            else if (i.severity == "WARNING") ++warnings;
            else if (i.severity == "OK") ++ok;
        }

        if (!opt.quiet) {
            std::cout << "Themis Safe Move Validator\n";
            std::cout << "Plan: " << opt.planCsv << "\n";
            std::cout << "Rows checked: " << selectedRows << "\n";
            std::cout << "OK rows: " << ok << "\n";
            std::cout << "Warnings: " << warnings << "\n";
            std::cout << "Errors: " << errors << "\n\n";

            for (const auto& i : issues) {
                if (i.severity == "OK") continue;
                std::cout << i.severity << " row " << i.rowNumber << " [" << i.code << "] " << i.message << "\n";
                std::cout << "  source: " << i.source << "\n";
                std::cout << "  dest:   " << i.destination << "\n";
            }
        } else {
            std::cout << "OK=" << ok << " WARNINGS=" << warnings << " ERRORS=" << errors << "\n";
        }

        if (!opt.reportCsv.empty()) {
            write_report_csv(opt.reportCsv, issues);
            if (!opt.quiet) std::cout << "\nReport written: " << opt.reportCsv << "\n";
        }

        if (errors > 0) return 1;
        if (opt.strict && warnings > 0) return 1;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 2;
    }
}
