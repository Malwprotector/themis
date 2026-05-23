#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <vector>
#include <string>

namespace fs = std::filesystem;

HWND hList;
HWND hEdit;
HWND hScan;
HWND hApply;

std::vector<std::string> files;

void scanFolder(const std::string& path) {
    files.clear();

    for (auto& p : fs::recursive_directory_iterator(path)) {
        if (p.is_regular_file()) {
            files.push_back(p.path().string());
        }
    }

    SendMessage(hList, LB_RESETCONTENT, 0, 0);

    for (auto& f : files) {
        SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)f.c_str());
    }
}

std::string selectFolder(HWND hwnd) {
    BROWSEINFO bi = {0};
    bi.lpszTitle = "Select folder";

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);

    char path[MAX_PATH];
    if (pidl && SHGetPathFromIDList(pidl, path)) {
        return std::string(path);
    }

    return "";
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static std::string currentPath;

    switch(msg) {
        case WM_CREATE:
            hEdit = CreateWindow("EDIT", "",
                WS_VISIBLE | WS_CHILD | WS_BORDER,
                10, 10, 500, 25,
                hwnd, NULL, NULL, NULL);

            CreateWindow("BUTTON", "Select",
                WS_VISIBLE | WS_CHILD,
                520, 10, 80, 25,
                hwnd, (HMENU)1, NULL, NULL);

            hScan = CreateWindow("BUTTON", "Scan",
                WS_VISIBLE | WS_CHILD,
                610, 10, 80, 25,
                hwnd, (HMENU)2, NULL, NULL);

            hApply = CreateWindow("BUTTON", "Apply",
                WS_VISIBLE | WS_CHILD,
                700, 10, 80, 25,
                hwnd, (HMENU)3, NULL, NULL);

            hList = CreateWindow("LISTBOX", NULL,
                WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL,
                10, 50, 770, 400,
                hwnd, NULL, NULL, NULL);
            break;

        case WM_COMMAND:
            switch(LOWORD(wParam)) {
                case 1: {
                    currentPath = selectFolder(hwnd);
                    SetWindowText(hEdit, currentPath.c_str());
                    break;
                }
                case 2: {
                    char path[512];
                    GetWindowText(hEdit, path, 512);

                    scanFolder(path);
                    break;
                }
                case 3: {
                    MessageBox(hwnd, "Apply logic goes here", "Apply", MB_OK);
                    break;
                }
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const char CLASS_NAME[] = "ThemisGUI";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        "Themis GUI (C++)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 520,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}