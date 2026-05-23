#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <random>

namespace fs = std::filesystem;

std::vector<std::string> extensions = {
    ".pdf", ".png", ".jpg", ".txt", ".docx",
    ".xlsx", ".csv", ".json", ".log", ".md", ".zip"
};

std::vector<std::string> words = {
    "report", "invoice", "scan", "backup", "data", "image",
    "notes", "draft", "final", "copy", "export", "tmp",
    "file", "document", "archive", "session", "log"
};

std::random_device rd;
std::mt19937 gen(rd());

int randInt(int min, int max) {
    std::uniform_int_distribution<> dist(min, max);
    return dist(gen);
}

std::string randomChoice(const std::vector<std::string>& v) {
    return v[randInt(0, v.size() - 1)];
}

std::string randomName() {
    int count = randInt(2, 5);
    std::string name;
    for (int i = 0; i < count; i++) {
        name += randomChoice(words);
        if (i < count - 1) name += "_";
    }
    return name;
}

std::string randomExtension() {
    return randomChoice(extensions);
}

std::string fakeContent(const std::string& ext) {
    if (ext == ".txt" || ext == ".md" || ext == ".log") {
        return "temporary log data - " + randomName();
    }
    if (ext == ".json") {
        return "{\"status\":\"ok\",\"data\":[]}";
    }
    if (ext == ".csv") {
        return "id,name,value\n1,test,0";
    }
    return "";
}

int main() {
    std::string baseDir = "chaos_output";

    fs::create_directory(baseDir);

    std::vector<fs::path> folders;
    folders.push_back(baseDir);

    for (int i = 0; i < 20; i++) {
        fs::path parent = folders[randInt(0, folders.size() - 1)];
        fs::path newDir = parent / randomName();
        fs::create_directories(newDir);
        folders.push_back(newDir);
    }

for (int i = 0; i < 1037; i++) {  // nbr max

    std::string ext = randomExtension();
    fs::path folder;

    if (randInt(0, 100) < 40) { //% fichiers racine
        folder = baseDir;
    } else {
        folder = folders[randInt(0, folders.size() - 1)];
    }

    fs::path filePath = folder / (randomName() + ext);

    std::ofstream file(filePath);
    file << fakeContent(ext);
}

    std::cout << "Chaos generated in: " << baseDir << std::endl;
    return 0;
}
