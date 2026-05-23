<table>
<tr>
<td><img src="https://github.com/Malwprotector/themis/blob/main/themis.jpg?raw=true" width="250"></td>

<td style="vertical-align: top; font-size:12px;">
<pre style="margin:0; white-space: pre-wrap;">
$ python3 -m themis --help
usage: themis [-h] {gui,scan,apply} ...

Thémis - file sorting assistant based on filename topics

positional arguments:
  {gui,scan,apply}
    gui             open graphical interface
    scan            scan folders and create a proposed sorting plan
    apply           apply a reviewed CSV plan

options:
  -h, --help        show this help message and exit
$
</pre>
</td>

</tr>
</table>

# Thémis

**Private, local, low-resource file sorting assistant powered by a lightweight LDA-inspired topic model.**

Thémis is a cross-platform Python application that helps users reorganize files on their own computer. It analyzes file names, proposes a new folder structure, shows the proposed changes before anything is moved, and lets the user edit the plan before applying it.

The application can be used either with a graphical interface or from the command line.

---

## Project Status At A Glance

| Property | Meaning |
|---|---|
| Name | **Thémis** |
| License | **GNU Affero General Public License** |
| Privacy model | **Local-first**. The application analyzes local file names on the user’s machine. |
| Network use | No network connection is required for normal use. |
| Data sent to third parties | None by design. |
| Main input | File names and paths selected by the user. |
| Main output | A proposed file-moving plan and, after confirmation, moved files. |
| Destructive operations | No deletion is performed by design. |
| Default behavior | Preview-first: Thémis proposes a plan before moving files. |
| Resource usage | Lightweight. It uses a small topic model over file-name tokens, not full file contents. |
| Cross-platform | Windows, macOS, and GNU/Linux. |
| Interfaces | GUI and CLI. |
| Target users | People who want to reorganize local folders safely and privately. |

---

## Meaning Of The Name

**Thémis** is named after **Themis**, the ancient Greek personification of divine order, law, fairness, and proper arrangement.

The name was chosen because the software’s purpose is to restore order in chaotic folders.

---

## What Thémis Does

Thémis helps sort local files by analyzing their file names.

For example, a folder containing:

```text
invoice_client_alpha_2025.pdf
invoice_client_beta_2024.pdf
holiday_family_photo.jpg
project_report_ai.docx
meeting_notes_budget.txt
```

may be reorganized into folders such as:

```text
Themis_Sorted/
  01_invoice_client_2025/
    invoice_client_alpha_2025.pdf
    invoice_client_beta_2024.pdf
  02_photo_holiday_family/
    holiday_family_photo.jpg
  03_project_report_ai/
    project_report_ai.docx
  04_meeting_budget_notes/
    meeting_notes_budget.txt
```

The exact result depends on the topic model, the number of requested topics, and the file names found in the selected folders.

---

## What Thémis Does Not Do

Thémis does not:

- read the full content of files;
- upload files to a server;
- require an online account;
- delete files;
- guarantee perfect classification;
- replace human validation;
- understand private business context unless that context appears in file names;
- decrypt, inspect, or modify document contents.

The program is designed as a local assistant, not as an autonomous document manager.

---

## Core Workflow

Thémis follows a review-first workflow.

```text
Select folders
      ↓
Scan file names
      ↓
Tokenize names
      ↓
Run LDA-inspired topic model
      ↓
Generate proposed folder structure
      ↓
Show editable plan
      ↓
User validates or edits
      ↓
Move selected files
      ↓
Write move history
```

No move is applied until the user confirms the operation.

---

## How The Algorithm Works

Thémis uses a lightweight LDA-inspired topic model adapted from the original `LDA.py` prototype.

### 1. Each File Name Becomes A Document

A file name such as:

```text
invoice_client_alpha_2025.pdf
```

is transformed into tokens:

```text
invoice, client, alpha, 2025
```

The extension may be used as fallback information if the name contains no useful token.

### 2. Stopwords Are Removed

Common words such as `the`, `and`, `de`, `le`, `la`, `document`, `file`, or `copy` are ignored because they usually do not help classification.

### 3. Topics Are Learned

The LDA model groups words that often appear in related file names. Each topic is represented by a small set of important words.

Example topic labels:

```text
invoice_client_2025
photo_holiday_family
project_report_ai
```

### 4. Each File Gets A Dominant Topic

For every file, Thémis computes the topic that best matches its file-name tokens.

### 5. A Destination Folder Is Proposed

The dominant topic becomes a proposed destination folder:

```text
Themis_Sorted/01_invoice_client_2025/my_file.pdf
```

---

## Repository Structure

```text
Thémis/
  README.md
  run_themis.py
  create.cpp
  themis/
    __init__.py
    cli.py
    gui.py
    lda_model.py
    scanner.py
```

>Note : create.cpp is a c++ program to "simulate" a chaotic file structure. to change the amount of file generated, edit `for (int i = 0; i < 100; i++) {` value. You can compile this program with g++ on linux : `g++ create.cpp -o chaos -std=c++17` .

### `run_themis.py`

Convenience launcher for the graphical interface.

Equivalent to:

```bash
python -m themis gui
```

### `themis/__init__.py`

Defines basic package metadata such as application name and version.

### `themis/lda_model.py`

Contains the lightweight LDA model.

Main responsibilities:

- store topic counts;
- store word-topic counts;
- run Gibbs sampling;
- compute document-topic distributions;
- find the dominant topic of each file;
- generate human-readable topic labels.

### `themis/scanner.py`

Contains the file scanning and planning logic.

Main responsibilities:

- recursively list files;
- ignore hidden files unless requested;
- tokenize file names;
- remove stopwords;
- build the proposed move plan;
- create safe destination paths;
- write and read CSV plans;
- apply selected file moves;
- write move history.

### `themis/gui.py`

Contains the Tkinter graphical interface.

Main responsibilities:

- add source directories;
- choose target root;
- select topic count;
- run analysis;
- display the proposed plan;
- edit destinations;
- toggle selected files;
- apply selected moves after confirmation.

### `themis/cli.py`

Contains the command-line interface.

Main commands:

```bash
python -m themis gui
python -m themis scan <directories>
python -m themis apply <plan.csv>
```

---

## Requirements

### Runtime Requirements

- Python 3.10 or newer is recommended.
- Tkinter is required for GUI mode.
- No mandatory third-party Python package is required for the current version.

### Optional Dependencies

NLTK can be installed to improve stopword handling:

```bash
pip install nltk
```

If NLTK or its stopword corpus is unavailable, Thémis automatically falls back to a built-in stopword list.

### Packaging Requirements

To compile standalone executables, install PyInstaller:

```bash
pip install pyinstaller
```

Important: PyInstaller is not a true cross-compiler. Build Windows binaries on Windows, macOS binaries on macOS, and Linux binaries on Linux.

---

## Installation From Source

### 1. Download Or Clone The Project

```bash
git clone https://example.com/themis.git
cd themis
```

If you downloaded a ZIP archive, extract it and open a terminal inside the extracted folder.

### 2. Create A Virtual Environment

```bash
python -m venv .venv
```

### 3. Activate The Virtual Environment

Windows PowerShell:

```powershell
.\.venv\Scripts\Activate.ps1
```

Windows Command Prompt:

```cmd
.venv\Scripts\activate.bat
```

macOS / Linux:

```bash
source .venv/bin/activate
```

### 4. Upgrade Packaging Tools

```bash
python -m pip install --upgrade pip setuptools wheel
```

### 5. Optional Dependencies

```bash
pip install nltk
```

### 6. Run Thémis

GUI:

```bash
python run_themis.py
```

CLI:

```bash
python -m themis --help
```

---

## GUI Usage Tutorial

### Step 1: Start The Application

```bash
python run_themis.py
```

The main window opens with the title:

```text
Thémis - File Sorting Assistant
```

### Step 2: Add Directories

Click:

```text
Add directory
```

Choose one or more folders containing files to sort.

### Step 3: Choose Topic Count

Use the `Topics` input.

Suggested values:

| Number Of Files | Suggested Topics |
|---:|---:|
| 10–50 | 3–6 |
| 50–500 | 6–12 |
| 500+ | 10–25 |

A higher topic count creates more specific folders. A lower topic count creates broader folders.

### Step 4: Choose Target Root

You can choose a target root folder.

If no target root is selected, Thémis creates a default folder inside the first selected directory:

```text
Themis_Sorted
```

### Step 5: Analyze

Click:

```text
Analyze
```

Thémis scans file names and proposes destinations.

### Step 6: Review The Plan

The table shows:

| Column | Meaning |
|---|---|
| Selected | Whether the file will be moved. |
| Source | Current file path. |
| Destination | Proposed new path. |
| Topic | Numeric topic identifier. |
| Confidence | Topic confidence score. |
| Reason | Tokens used to classify the file. |

### Step 7: Edit The Plan

You can:

- double-click a row to edit the destination;
- select a row and click `Edit destination`;
- select a row and click `Toggle selected` to include or exclude it.

### Step 8: Apply Selected Moves

Click:

```text
Apply selected moves
```

Confirm the operation. Thémis moves only selected files.

---

## CLI Usage Tutorial

### Show Help

```bash
python -m themis --help
```

### Open GUI From CLI

```bash
python -m themis gui
```

### Create A Sorting Plan

```bash
python -m themis scan ~/Downloads ~/Documents --topics 8 --output themis_plan.csv
```

This scans the folders and writes a CSV plan. It does not move files.

### Choose A Target Folder

```bash
python -m themis scan ~/Downloads --target ~/SortedFiles --topics 6 --output plan.csv
```

### Disable Recursive Scan

```bash
python -m themis scan ~/Downloads --no-recursive --output plan.csv
```

### Include Hidden Files

```bash
python -m themis scan ~/Downloads --include-hidden --output plan.csv
```

### Apply A Reviewed Plan

```bash
python -m themis apply plan.csv
```

### Direct Scan And Apply

Use with caution:

```bash
python -m themis scan ~/Downloads --topics 6 --apply
```

The safer workflow is to generate a CSV, review it, then apply it.

---

## CSV Plan Format

The generated CSV contains:

```text
selected,source,destination,topic,topic_label,confidence,reason
```

Example row:

```csv
true,/home/user/Downloads/invoice_alpha.pdf,/home/user/Downloads/Themis_Sorted/01_invoice_alpha/invoice_alpha.pdf,1,invoice_alpha,0.8462,"dominant topic from filename tokens: invoice, alpha"
```

### Editing The CSV Manually

Before applying a plan, you may edit:

- `selected`: set to `true` or `false`;
- `destination`: change the destination path;
- other columns are informational and should normally remain unchanged.

---


### History Log

Applied moves are logged in:

```text
themis_history.jsonl
```

Each line is a JSON object containing the source, destination, topic, confidence, and timestamp.

Example:

```json
{"selected": true, "source": "/old/file.pdf", "destination": "/new/file.pdf", "topic": 1, "topic_label": "invoice_client", "confidence": 0.8125, "reason": "dominant topic from filename tokens: invoice, client", "applied_at": "2026-05-22T21:30:00"}
```

---

## Build And Compile Guide

This section explains how to package Thémis as a standalone application using PyInstaller.

### General Build Notes

Install PyInstaller:

```bash
pip install pyinstaller
```

Recommended build modes:

| Mode | Command Option | Description |
|---|---|---|
| One-folder | `--onedir` | Creates a folder containing the executable and dependencies. Recommended for reliability. |
| One-file | `--onefile` | Creates a single executable. Easier to distribute, but startup can be slower. |
| Windowed | `--windowed` | Hides the terminal window for GUI builds. |
| Named app | `--name Themis` | Sets output executable or app name. |

Recommended entry point:

```text
run_themis.py
```

---

## Build On Windows

### 1. Install Python

Install Python 3.10 or newer from the official Python website or Microsoft Store.

During installation, enable:

```text
Add Python to PATH
```

### 2. Open PowerShell

Go to the project directory:

```powershell
cd path\to\themis
```

### 3. Create And Activate Virtual Environment

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
```

If script execution is blocked, run PowerShell as administrator or use:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

### 4. Install Build Tools

```powershell
python -m pip install --upgrade pip setuptools wheel
pip install pyinstaller
```

Optional:

```powershell
pip install nltk
```

### 5. Run From Source

```powershell
python run_themis.py
```

### 6. Build One-Folder Executable

```powershell
pyinstaller --noconfirm --clean --onedir --windowed --name Themis run_themis.py
```

Output:

```text
dist\Themis\Themis.exe
```

### 7. Build One-File Executable

```powershell
pyinstaller --noconfirm --clean --onefile --windowed --name Themis run_themis.py
```

Output:

```text
dist\Themis.exe
```

### 8. Run The Build

```powershell
.\dist\Themis\Themis.exe
```

or for one-file mode:

```powershell
.\dist\Themis.exe
```

---

## Build On macOS

### 1. Install Python

Install Python 3.10 or newer.

Using Homebrew:

```bash
brew install python
```

### 2. Open Terminal

```bash
cd /path/to/themis
```

### 3. Create And Activate Virtual Environment

```bash
python3 -m venv .venv
source .venv/bin/activate
```

### 4. Install Build Tools

```bash
python -m pip install --upgrade pip setuptools wheel
pip install pyinstaller
```

Optional:

```bash
pip install nltk
```

### 5. Run From Source

```bash
python run_themis.py
```

### 6. Build macOS App Bundle

```bash
pyinstaller --noconfirm --clean --windowed --name Themis run_themis.py
```

Output:

```text
dist/Themis.app
```

### 7. Run The App

```bash
open dist/Themis.app
```

### 8. Build One-File CLI/GUI Binary

```bash
pyinstaller --noconfirm --clean --onefile --windowed --name Themis run_themis.py
```

Output:

```text
dist/Themis
```

### 9. Gatekeeper Notes

Unsigned macOS applications may be blocked by Gatekeeper. For local testing, you can right-click the app and choose `Open`.

For public distribution, use proper Apple code signing and notarization.

---

## Build On GNU/Linux

### 1. Install Python And Tkinter

Debian / Ubuntu:

```bash
sudo apt update
sudo apt install python3 python3-venv python3-pip python3-tk binutils
```

Fedora:

```bash
sudo dnf install python3 python3-pip python3-tkinter binutils
```

Arch Linux:

```bash
sudo pacman -S python python-pip tk binutils
```

### 2. Open Terminal

```bash
cd /path/to/themis
```

### 3. Create And Activate Virtual Environment

```bash
python3 -m venv .venv
source .venv/bin/activate
```

### 4. Install Build Tools

```bash
python -m pip install --upgrade pip setuptools wheel
pip install pyinstaller
```

Optional:

```bash
pip install nltk
```

### 5. Run From Source

```bash
python run_themis.py
```

### 6. Build One-Folder Application

```bash
pyinstaller --noconfirm --clean --onedir --windowed --name Themis run_themis.py
```

Output:

```text
dist/Themis/Themis
```

Run:

```bash
./dist/Themis/Themis
```

### 7. Build One-File Executable

```bash
pyinstaller --noconfirm --clean --onefile --windowed --name Themis run_themis.py
```

Output:

```text
dist/Themis
```

Run:

```bash
./dist/Themis
```

### 8. AppImage / Deb / RPM Packaging

PyInstaller creates binaries, not full native packages. To create Linux packages, use an additional packaging tool after building:

- AppImage: `appimagetool`
- Debian package: `dpkg-deb`, `fpm`, or Debian packaging tools
- RPM package: `rpmbuild` or `fpm`

Keep the AGPL license file and corresponding source code with any distributed package.

---

## Development Workflow

### Run GUI

```bash
python run_themis.py
```

### Run CLI

```bash
python -m themis scan ./test_files --topics 5 --output plan.csv
```

### Apply Plan

```bash
python -m themis apply plan.csv
```


## Troubleshooting


### GUI Does Not Start On Linux

Install Tkinter:

```bash
sudo apt install python3-tk
```

### PyInstaller Command Not Found

Use:

```bash
python -m PyInstaller --version
```

or reinstall:

```bash
pip install --upgrade pyinstaller
```

### The Build Works On One OS But Not Another

Build separately on each target operating system. Do not expect a Windows executable built on Linux to work as a native Windows build.

### Classification Is Poor

Try:

- reducing the number of topics;
- increasing the number of topics;
- renaming unclear files before scanning;
- sorting a smaller folder first;
- using more descriptive file names.

### Some Files Were Not Moved

Possible causes:

- missing permissions;
- file currently open in another program;
- source file removed after plan creation;
- destination drive unavailable;
- synchronized folder conflict.

---

## Limitations

- File classification is based mainly on file names.
- Very short or generic names are difficult to classify.
- LDA works better when there are enough files and repeated naming patterns.
- The current GUI is intentionally simple.
- The software does not yet provide a full rollback button, although moves are logged.
- Packaging and signing must be handled separately for production distribution.

---

## Roadmap Ideas

Possible future improvements:

- full undo / rollback interface;
- drag-and-drop directory selection;
- richer preview tree;
- extension-aware sorting rules;
- date-aware sorting rules;
- file metadata support;
- duplicate detection;
- export to JSON and YAML;
- saved sorting profiles;
- optional advanced NLP backend;
- signed installers for Windows and macOS;
- AppImage, `.deb`, and `.rpm` builds for Linux.

---

## License

Thémis is licensed under:

```text
GNU Affero General Public License
```


## Disclaimer

Thémis is provided without warranty. Use it carefully, especially on important folders. Always review the proposed plan before applying file moves.

The license summary in this README is provided for convenience and is not legal advice. The actual license text controls.
