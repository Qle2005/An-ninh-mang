#include "gui_app.h"

#include "aes.h"
#include "file_utils.h"

#include <commdlg.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <shlobj.h>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr int ID_INPUT = 101;
constexpr int ID_OUTPUT = 102;
constexpr int ID_KEY = 103;
constexpr int ID_RESULT = 104;
constexpr int ID_LBL_INPUT = 111;
constexpr int ID_LBL_KEY = 112;
constexpr int ID_LBL_RESULT = 113;
constexpr int ID_TAB_ENCRYPT = 120;
constexpr int ID_TAB_DECRYPT = 121;
constexpr int ID_BTN_BROWSE_INPUT = 201;
constexpr int ID_BTN_BROWSE_OUTPUT = 202;
constexpr int ID_BTN_GEN_KEY = 203;
constexpr int ID_BTN_ENCRYPT = 204;
constexpr int ID_BTN_DECRYPT = 205;
constexpr int ID_BTN_COPY = 206;
constexpr int ID_BTN_EXPORT_FILE = 207;

enum class Mode { Encrypt, Decrypt };
Mode g_mode = Mode::Encrypt;
HFONT g_titleFont = nullptr;
HFONT g_normalFont = nullptr;
HBRUSH g_bgBrush = nullptr;
HBRUSH g_panelBrush = nullptr;

void copyTextToClipboard(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], size, nullptr, nullptr);
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], size);
    return out;
}

std::wstring lowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value;
}

bool isSupportedSourceExtension(const std::wstring& path) {
    std::wstring ext = lowerWide(std::filesystem::path(path).extension().wstring());
    return ext == L".txt" || ext == L".docx" || ext == L".xlsx" || ext == L".pdf";
}

bool fileExists(const std::wstring& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(path), ec);
}

std::wstring supportedSourceText() {
    return L"Chi ho tro file .txt, .docx (Word), .xlsx (Excel), .pdf.";
}

std::string bytesToHex(const std::vector<uint8_t>& data) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        out.push_back(digits[(byte >> 4) & 0x0F]);
        out.push_back(digits[byte & 0x0F]);
    }
    return out;
}

int hexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool hexToBytes(const std::string& hex, std::vector<uint8_t>& out) {
    std::string compact;
    compact.reserve(hex.size());
    for (unsigned char ch : hex) {
        if (!std::isspace(ch)) compact.push_back(static_cast<char>(ch));
    }
    if (compact.empty() || compact.size() % 2 != 0) return false;

    out.clear();
    out.reserve(compact.size() / 2);
    for (size_t i = 0; i < compact.size(); i += 2) {
        int high = hexNibble(compact[i]);
        int low = hexNibble(compact[i + 1]);
        if (high < 0 || low < 0) return false;
        out.push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return true;
}

std::wstring powershellQuote(const std::wstring& value) {
    std::wstring quoted = L"'";
    for (wchar_t ch : value) {
        quoted += ch;
        if (ch == L'\'') quoted += L'\'';
    }
    quoted += L"'";
    return quoted;
}

bool runHiddenProcess(std::wstring commandLine) {
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::vector<wchar_t> cmd(commandLine.begin(), commandLine.end());
    cmd.push_back(L'\0');
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exitCode == 0;
}

std::wstring makeTempTextPath() {
    wchar_t tempDir[MAX_PATH] = {0};
    wchar_t tempFile[MAX_PATH] = {0};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) return L"";
    if (GetTempFileNameW(tempDir, L"aes", 0, tempFile) == 0) return L"";
    return tempFile;
}

void trimUtf8Bom(std::vector<uint8_t>& data) {
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data.erase(data.begin(), data.begin() + 3);
    }
}

bool extractOfficeOpenXmlText(const std::wstring& inputPath, bool excel, std::string& outText) {
    std::wstring tempPath = makeTempTextPath();
    if (tempPath.empty()) return false;

    std::wstring selector = excel
        ? L"($entry.FullName -eq 'xl/sharedStrings.xml' -or $entry.FullName -like 'xl/worksheets/*.xml')"
        : L"($entry.FullName -eq 'word/document.xml' -or $entry.FullName -like 'word/header*.xml' -or $entry.FullName -like 'word/footer*.xml' -or $entry.FullName -eq 'word/footnotes.xml' -or $entry.FullName -eq 'word/endnotes.xml')";

    std::wstring script =
        L"& {"
        L"Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        L"$p=" + powershellQuote(inputPath) + L";"
        L"$o=" + powershellQuote(tempPath) + L";"
        L"$zip=[System.IO.Compression.ZipFile]::OpenRead($p);"
        L"try {"
        L"$items=New-Object System.Collections.Generic.List[string];"
        L"foreach($entry in $zip.Entries){"
        L"if(" + selector + L"){"
        L"$stream=$entry.Open();"
        L"try {"
        L"$reader=New-Object System.IO.StreamReader($stream,[System.Text.Encoding]::UTF8,$true);"
        L"$xml=$reader.ReadToEnd();"
        L"$xml=$xml -replace '</w:p>|</row>|</si>|</c>|</v>|</t>',' ';"
        L"$xml=$xml -replace '<[^>]+>',' ';"
        L"$xml=[System.Net.WebUtility]::HtmlDecode($xml);"
        L"$xml=($xml -replace '\\s+',' ').Trim();"
        L"if($xml.Length -gt 0){[void]$items.Add($xml)}"
        L"} finally {$stream.Dispose()}"
        L"}"
        L"}"
        L"[System.IO.File]::WriteAllText($o,($items -join [Environment]::NewLine),[System.Text.UTF8Encoding]::new($false));"
        L"} finally {$zip.Dispose()}"
        L"}";

    std::wstring commandLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command " + script;
    bool ok = runHiddenProcess(commandLine);
    std::vector<uint8_t> data = ok ? readBinaryFile(tempPath) : std::vector<uint8_t>{};
    DeleteFileW(tempPath.c_str());
    if (data.empty()) return false;
    trimUtf8Bom(data);
    outText.assign(data.begin(), data.end());
    return !outText.empty();
}

bool mostlyReadable(const std::string& text) {
    if (text.empty()) return false;
    size_t readable = 0;
    for (unsigned char ch : text) {
        if (ch == '\r' || ch == '\n' || ch == '\t' || (ch >= 32 && ch < 127) || ch >= 128) {
            ++readable;
        }
    }
    return readable * 100 / text.size() >= 70;
}

std::string decodePdfLiteralString(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        char ch = raw[i];
        if (ch != '\\' || i + 1 >= raw.size()) {
            out.push_back(ch);
            continue;
        }

        char next = raw[++i];
        switch (next) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case '(':
            case ')':
            case '\\': out.push_back(next); break;
            default:
                if (next >= '0' && next <= '7') {
                    int value = next - '0';
                    int count = 1;
                    while (count < 3 && i + 1 < raw.size() && raw[i + 1] >= '0' && raw[i + 1] <= '7') {
                        value = value * 8 + (raw[++i] - '0');
                        ++count;
                    }
                    out.push_back(static_cast<char>(value));
                } else {
                    out.push_back(next);
                }
                break;
        }
    }
    return out;
}

bool extractPdfText(const std::wstring& inputPath, std::string& outText) {
    std::vector<uint8_t> data = readBinaryFile(inputPath);
    if (data.empty()) return false;

    std::string bytes(data.begin(), data.end());
    std::ostringstream text;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] != '(') continue;

        std::string raw;
        int depth = 1;
        bool escaped = false;
        for (++i; i < bytes.size() && depth > 0; ++i) {
            char ch = bytes[i];
            if (escaped) {
                raw.push_back('\\');
                raw.push_back(ch);
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '(') {
                ++depth;
                raw.push_back(ch);
            } else if (ch == ')') {
                --depth;
                if (depth > 0) raw.push_back(ch);
            } else {
                raw.push_back(ch);
            }
        }

        std::string decoded = decodePdfLiteralString(raw);
        if (decoded.size() >= 2 && mostlyReadable(decoded)) {
            text << decoded << '\n';
        }
    }

    outText = text.str();
    return !outText.empty();
}

bool extractTextContent(const std::wstring& inputPath, std::vector<uint8_t>& outContent) {
    std::wstring ext = lowerWide(std::filesystem::path(inputPath).extension().wstring());
    std::string text;

    if (ext == L".txt") {
        outContent = readBinaryFile(inputPath);
        trimUtf8Bom(outContent);
        return !outContent.empty();
    }

    if (ext == L".docx" || ext == L".xlsx") {
        if (!extractOfficeOpenXmlText(inputPath, ext == L".xlsx", text)) return false;
    } else if (ext == L".pdf") {
        if (!extractPdfText(inputPath, text)) return false;
    } else {
        return false;
    }

    outContent.assign(text.begin(), text.end());
    return !outContent.empty();
}

std::wstring getEditText(HWND parent, int id) {
    HWND h = GetDlgItem(parent, id);
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return L"";
    std::wstring text(static_cast<size_t>(len), L'\0');
    GetWindowTextW(h, text.data(), len + 1);
    return text;
}

void setEditText(HWND parent, int id, const std::wstring& value) {
    SetWindowTextW(GetDlgItem(parent, id), value.c_str());
}

void setControlFont(HWND parent, int id, HFONT font) {
    SendMessageW(GetDlgItem(parent, id), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

bool chooseInputFile(HWND owner, std::wstring& outPath) {
    wchar_t pathBuffer[MAX_PATH] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = pathBuffer;
    ofn.nMaxFile = MAX_PATH;
    if (g_mode == Mode::Encrypt) {
        ofn.lpstrFilter = L"Supported Text Files (*.txt;*.docx;*.xlsx;*.pdf)\0*.txt;*.docx;*.xlsx;*.pdf\0"
                           L"Text (*.txt)\0*.txt\0Word (*.docx)\0*.docx\0Excel (*.xlsx)\0*.xlsx\0PDF (*.pdf)\0*.pdf\0";
    } else {
        ofn.lpstrFilter = L"Encrypted Files (*.txt;*.docx;*.xlsx;*.pdf;*.aes)\0*.txt;*.docx;*.xlsx;*.pdf;*.aes\0"
                           L"All Files\0*.*\0";
    }
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    BOOL ok = GetOpenFileNameW(&ofn);

    if (!ok) return false;
    outPath = pathBuffer;
    return true;
}

bool chooseOutputFolder(HWND owner, std::wstring& outFolder) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Chon thu muc output";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;

    wchar_t path[MAX_PATH] = {0};
    bool ok = SHGetPathFromIDListW(pidl, path) == TRUE;
    CoTaskMemFree(pidl);
    if (!ok) return false;

    outFolder = path;
    return true;
}

std::wstring getContainingFolder(const std::wstring& inputFile) {
    return std::filesystem::path(inputFile).parent_path().wstring();
}

std::wstring buildOutputPath(const std::wstring& inputFile, const std::wstring& outputFolder, bool encryptMode) {
    namespace fs = std::filesystem;
    fs::path inPath(inputFile);
    fs::path folder(outputFolder);
    std::wstring stem = inPath.stem().wstring();
    std::wstring name = encryptMode
        ? (stem + L"_encrypted" + inPath.extension().wstring())
        : (stem + L"_decrypted.bin");
    return (folder / name).wstring();
}

std::wstring buildDecryptedOutputPath(const std::wstring& inputFile, const std::wstring& outputFolder, const std::wstring& originalExt) {
    namespace fs = std::filesystem;
    fs::path inPath(inputFile);
    fs::path folder(outputFolder);
    std::wstring stem = inPath.stem().wstring();
    std::wstring ext = originalExt;
    if (!ext.empty() && ext[0] != L'.') {
        ext.insert(ext.begin(), L'.');
    }
    std::wstring name = stem + L"_decrypted" + ext;
    return (folder / name).wstring();
}

std::wstring buildTextDecryptedOutputPath(const std::wstring& inputFile, const std::wstring& outputFolder) {
    namespace fs = std::filesystem;
    fs::path inPath(inputFile);
    fs::path folder(outputFolder);
    std::wstring name = inPath.stem().wstring() + L"_decrypted_text.txt";
    return (folder / name).wstring();
}

void showInfo(HWND owner, const wchar_t* text) {
    MessageBoxW(owner, text, L"Thong bao", MB_OK | MB_ICONINFORMATION);
}

void showError(HWND owner, const wchar_t* text) {
    MessageBoxW(owner, text, L"Loi", MB_OK | MB_ICONERROR);
}

void updateModeUi(HWND hwnd) {
    for (int id : {ID_OUTPUT, ID_BTN_BROWSE_OUTPUT, ID_BTN_EXPORT_FILE}) {
        ShowWindow(GetDlgItem(hwnd, id), SW_HIDE);
    }
    for (int id : {ID_LBL_RESULT, ID_RESULT, ID_BTN_COPY}) {
        ShowWindow(GetDlgItem(hwnd, id), SW_SHOW);
    }

    if (g_mode == Mode::Encrypt) {
        SetWindowTextW(GetDlgItem(hwnd, ID_LBL_INPUT), L"Van ban goc:");
        SetWindowTextW(GetDlgItem(hwnd, ID_LBL_RESULT), L"Van ban ma hoa:");
        SetWindowTextW(GetDlgItem(hwnd, ID_BTN_BROWSE_INPUT), L"Chon File");
        SetWindowTextW(GetDlgItem(hwnd, ID_BTN_BROWSE_OUTPUT), L"Cung Thu Muc");
        SetWindowTextW(GetDlgItem(hwnd, ID_BTN_ENCRYPT), L"Ma Hoa");
        SetWindowTextW(GetDlgItem(hwnd, ID_BTN_DECRYPT), L"Xuat Khoa");
        EnableWindow(GetDlgItem(hwnd, ID_BTN_GEN_KEY), TRUE);
        EnableWindow(GetDlgItem(hwnd, ID_OUTPUT), FALSE);
        EnableWindow(GetDlgItem(hwnd, ID_BTN_BROWSE_OUTPUT), FALSE);
        EnableWindow(GetDlgItem(hwnd, ID_BTN_EXPORT_FILE), FALSE);
        EnableWindow(GetDlgItem(hwnd, ID_TAB_ENCRYPT), FALSE);
        EnableWindow(GetDlgItem(hwnd, ID_TAB_DECRYPT), TRUE);
    } else {
        SetWindowTextW(GetDlgItem(hwnd, ID_LBL_INPUT), L"Van ban ma hoa:");
        SetWindowTextW(GetDlgItem(hwnd, ID_LBL_RESULT), L"Van ban giai ma:");
        SetWindowTextW(GetDlgItem(hwnd, ID_BTN_BROWSE_INPUT), L"Chon File Ciphertext");
        SetWindowTextW(GetDlgItem(hwnd, ID_BTN_BROWSE_OUTPUT), L"Cung Thu Muc");
        SetWindowTextW(GetDlgItem(hwnd, ID_BTN_ENCRYPT), L"Giai Ma");
        SetWindowTextW(GetDlgItem(hwnd, ID_BTN_DECRYPT), L"Xuat Khoa");
        EnableWindow(GetDlgItem(hwnd, ID_BTN_GEN_KEY), FALSE);
        EnableWindow(GetDlgItem(hwnd, ID_OUTPUT), FALSE);
        EnableWindow(GetDlgItem(hwnd, ID_BTN_BROWSE_OUTPUT), FALSE);
        EnableWindow(GetDlgItem(hwnd, ID_BTN_EXPORT_FILE), FALSE);
        EnableWindow(GetDlgItem(hwnd, ID_TAB_ENCRYPT), TRUE);
        EnableWindow(GetDlgItem(hwnd, ID_TAB_DECRYPT), FALSE);
    }
}

void handleEncrypt(HWND hwnd, bool generateKey) {
    std::wstring inputW = getEditText(hwnd, ID_INPUT);

    if (inputW.empty()) {
        showError(hwnd, L"Vui long chon file input.");
        return;
    }

    bool inputIsFile = fileExists(inputW);
    if (inputIsFile && !isSupportedSourceExtension(inputW)) {
        showError(hwnd, supportedSourceText().c_str());
        return;
    }

    std::vector<uint8_t> plain;
    if (inputIsFile) {
        if (!extractTextContent(inputW, plain)) {
            showError(hwnd, L"Khong trich xuat duoc noi dung van ban tu file input.");
            return;
        }
    } else {
        std::string typedText = narrow(inputW);
        plain.assign(typedText.begin(), typedText.end());
    }
    if (plain.empty()) {
        showError(hwnd, L"Khong trich xuat duoc noi dung van ban tu file input.");
        return;
    }

    aes::Key128 key{};
    if (generateKey) {
        key = aes::randomKey128();
        setEditText(hwnd, ID_KEY, widen(aes::toHex(key)));
    } else {
        std::wstring keyW = getEditText(hwnd, ID_KEY);
        if (!aes::parseHexKey(narrow(keyW), key)) {
            showError(hwnd, L"Key khong hop le. Key AES-128 phai la 32 ky tu hex.");
            return;
        }
    }

    std::string ext = ".txt";
    if (ext.size() > 255) {
        showError(hwnd, L"Duoi file qua dai de luu metadata.");
        return;
    }
    std::vector<uint8_t> payload;
    payload.reserve(5 + ext.size() + plain.size());
    payload.push_back('D');
    payload.push_back('T');
    payload.push_back('X');
    payload.push_back('1');
    payload.push_back(static_cast<uint8_t>(ext.size()));
    payload.insert(payload.end(), ext.begin(), ext.end());
    payload.insert(payload.end(), plain.begin(), plain.end());
    std::vector<uint8_t> encrypted = aes::cryptData(aes::pkcs7Pad(payload), key, false);

    if (inputIsFile) {
        if (!writeBinaryFile(inputW, encrypted)) {
            showError(hwnd, L"Khong ghi de duoc file input.");
            return;
        }
        setEditText(hwnd, ID_RESULT, L"");
        showInfo(hwnd, L"Ma hoa noi dung van ban thanh cong. File goc da duoc ghi de.");
    } else {
        setEditText(hwnd, ID_RESULT, widen(bytesToHex(encrypted)));
        showInfo(hwnd, L"Ma hoa van ban thanh cong. Khoa da duoc hien trong o Key.");
    }
}

void handleDecrypt(HWND hwnd) {
    std::wstring inputW = getEditText(hwnd, ID_INPUT);
    std::wstring keyW = getEditText(hwnd, ID_KEY);

    if (inputW.empty() || keyW.empty()) {
        showError(hwnd, L"Vui long nhap du input/key.");
        return;
    }

    aes::Key128 key{};
    if (!aes::parseHexKey(narrow(keyW), key)) {
        showError(hwnd, L"Key khong hop le. Key AES-128 phai la 32 ky tu hex.");
        return;
    }

    bool inputIsFile = fileExists(inputW);
    std::vector<uint8_t> encrypted;
    if (inputIsFile) {
        encrypted = readBinaryFile(inputW);
    } else if (!hexToBytes(narrow(inputW), encrypted)) {
        showError(hwnd, L"Van ban ma hoa nhap tay phai la chuoi hex hop le.");
        return;
    }
    if (encrypted.empty() || encrypted.size() % 16 != 0) {
        showError(hwnd, L"File ma hoa khong hop le.");
        return;
    }

    std::vector<uint8_t> decrypted = aes::cryptData(encrypted, key, true);
    if (!aes::pkcs7Unpad(decrypted)) {
        showError(hwnd, L"Giai ma that bai (sai key hoac du lieu hong).");
        return;
    }

    bool textPayload = false;
    std::wstring originalExt = L".bin";
    std::vector<uint8_t> content = decrypted;
    if (decrypted.size() >= 5 &&
        decrypted[0] == 'D' && decrypted[1] == 'E' &&
        decrypted[2] == 'X' && decrypted[3] == '1') {
        size_t extLen = decrypted[4];
        if (decrypted.size() >= 5 + extLen) {
            bool validExt = true;
            for (size_t i = 0; i < extLen; ++i) {
                if (decrypted[5 + i] == 0) {
                    validExt = false;
                    break;
                }
            }
            if (validExt) {
                std::string ext(reinterpret_cast<const char*>(&decrypted[5]), extLen);
                content.assign(decrypted.begin() + 5 + extLen, decrypted.end());
                originalExt = widen(ext);
                if (originalExt.empty()) {
                    originalExt = L".bin";
                }
            }
        }
    } else if (decrypted.size() >= 5 &&
        decrypted[0] == 'D' && decrypted[1] == 'T' &&
        decrypted[2] == 'X' && decrypted[3] == '1') {
        size_t extLen = decrypted[4];
        if (decrypted.size() >= 5 + extLen) {
            std::string ext(reinterpret_cast<const char*>(&decrypted[5]), extLen);
            content.assign(decrypted.begin() + 5 + extLen, decrypted.end());
            originalExt = widen(ext.empty() ? ".txt" : ext);
            textPayload = true;
        }
    }

    if (inputIsFile) {
        if (!writeBinaryFile(inputW, content)) {
            showError(hwnd, L"Khong ghi de duoc file input.");
            return;
        }
        setEditText(hwnd, ID_RESULT, L"");
        showInfo(hwnd, L"Giai ma thanh cong. File da duoc ghi de.");
    } else {
        std::string plainText(content.begin(), content.end());
        setEditText(hwnd, ID_RESULT, widen(plainText));
        showInfo(hwnd, L"Giai ma van ban thanh cong.");
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_bgBrush = CreateSolidBrush(RGB(236, 244, 253));
            g_panelBrush = CreateSolidBrush(RGB(230, 241, 251));
            g_titleFont = CreateFontW(46, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_normalFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

            CreateWindowW(L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_ETCHEDFRAME,
                          18, 70, 944, 618, hwnd, nullptr, nullptr, nullptr);
            CreateWindowW(L"STATIC", L"Thuat Toan AES - Ma Hoa - Giai Ma", WS_VISIBLE | WS_CHILD | SS_CENTER,
                          0, 20, 980, 42, hwnd, nullptr, nullptr, nullptr);

            CreateWindowW(L"BUTTON", L"Ma Hoa", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                          34, 92, 110, 44, hwnd, reinterpret_cast<HMENU>(ID_TAB_ENCRYPT), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Giai Ma", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                          146, 92, 110, 44, hwnd, reinterpret_cast<HMENU>(ID_TAB_DECRYPT), nullptr, nullptr);

            CreateWindowW(L"STATIC", L"Van ban goc:", WS_VISIBLE | WS_CHILD | SS_RIGHT,
                          42, 168, 130, 28, hwnd, reinterpret_cast<HMENU>(ID_LBL_INPUT), nullptr, nullptr);
            CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                          182, 162, 640, 38, hwnd, reinterpret_cast<HMENU>(ID_INPUT), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Chon File", WS_VISIBLE | WS_CHILD | BS_FLAT,
                          834, 162, 110, 38, hwnd, reinterpret_cast<HMENU>(ID_BTN_BROWSE_INPUT), nullptr, nullptr);

            CreateWindowW(L"STATIC", L"Khoa:", WS_VISIBLE | WS_CHILD | SS_RIGHT,
                          42, 217, 130, 28, hwnd, reinterpret_cast<HMENU>(ID_LBL_KEY), nullptr, nullptr);
            CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                          182, 212, 762, 38, hwnd, reinterpret_cast<HMENU>(ID_KEY), nullptr, nullptr);

            CreateWindowW(L"STATIC", L"Van ban ma hoa:", WS_VISIBLE | WS_CHILD | SS_RIGHT,
                          42, 268, 130, 28, hwnd, reinterpret_cast<HMENU>(ID_LBL_RESULT), nullptr, nullptr);
            CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                          182, 262, 668, 38, hwnd, reinterpret_cast<HMENU>(ID_RESULT), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Copy", WS_VISIBLE | WS_CHILD | BS_FLAT,
                          860, 262, 84, 38, hwnd, reinterpret_cast<HMENU>(ID_BTN_COPY), nullptr, nullptr);

            CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                          182, 312, 640, 38, hwnd, reinterpret_cast<HMENU>(ID_OUTPUT), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Chon Thu Muc", WS_VISIBLE | WS_CHILD | BS_FLAT,
                          834, 312, 110, 38, hwnd, reinterpret_cast<HMENU>(ID_BTN_BROWSE_OUTPUT), nullptr, nullptr);

            CreateWindowW(L"BUTTON", L"Tao Khoa", WS_VISIBLE | WS_CHILD | BS_FLAT,
                          34, 400, 910, 42, hwnd, reinterpret_cast<HMENU>(ID_BTN_GEN_KEY), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Ma Hoa", WS_VISIBLE | WS_CHILD | BS_FLAT,
                          34, 450, 910, 42, hwnd, reinterpret_cast<HMENU>(ID_BTN_ENCRYPT), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Xuat Khoa", WS_VISIBLE | WS_CHILD | BS_FLAT,
                          34, 500, 445, 42, hwnd, reinterpret_cast<HMENU>(ID_BTN_DECRYPT), nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Xuat File", WS_VISIBLE | WS_CHILD | BS_FLAT,
                          499, 500, 445, 42, hwnd, reinterpret_cast<HMENU>(ID_BTN_EXPORT_FILE), nullptr, nullptr);

            for (int id : {ID_INPUT, ID_OUTPUT, ID_KEY, ID_RESULT, ID_BTN_BROWSE_INPUT, ID_BTN_BROWSE_OUTPUT,
                           ID_BTN_GEN_KEY, ID_BTN_ENCRYPT, ID_BTN_DECRYPT, ID_BTN_COPY, ID_BTN_EXPORT_FILE,
                           ID_TAB_ENCRYPT, ID_TAB_DECRYPT,
                           ID_LBL_INPUT, ID_LBL_KEY, ID_LBL_RESULT}) {
                setControlFont(hwnd, id, g_normalFont);
            }
            HWND title = FindWindowExW(hwnd, nullptr, L"Static", L"Thuat Toan AES - Ma Hoa - Giai Ma");
            if (title) SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(g_titleFont), TRUE);

            updateModeUi(hwnd);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_TAB_ENCRYPT) {
                g_mode = Mode::Encrypt;
                updateModeUi(hwnd);
            } else if (id == ID_TAB_DECRYPT) {
                g_mode = Mode::Decrypt;
                updateModeUi(hwnd);
            } else if (id == ID_BTN_BROWSE_INPUT) {
                std::wstring p;
                if (chooseInputFile(hwnd, p)) {
                    setEditText(hwnd, ID_INPUT, p);
                    setEditText(hwnd, ID_OUTPUT, getContainingFolder(p));
                }
            } else if (id == ID_BTN_BROWSE_OUTPUT) {
                std::wstring p;
                if (chooseOutputFolder(hwnd, p)) setEditText(hwnd, ID_OUTPUT, p);
            } else if (id == ID_BTN_EXPORT_FILE) {
                std::wstring p;
                if (chooseOutputFolder(hwnd, p)) setEditText(hwnd, ID_OUTPUT, p);
            } else if (id == ID_BTN_GEN_KEY) {
                setEditText(hwnd, ID_KEY, widen(aes::toHex(aes::randomKey128())));
            } else if (id == ID_BTN_COPY) {
                copyTextToClipboard(hwnd, getEditText(hwnd, ID_RESULT));
            } else if (id == ID_BTN_ENCRYPT) {
                if (g_mode == Mode::Encrypt) {
                    handleEncrypt(hwnd, true);
                } else {
                    handleDecrypt(hwnd);
                }
            } else if (id == ID_BTN_DECRYPT) {
                std::wstring key = getEditText(hwnd, ID_KEY);
                if (key.empty()) {
                    showError(hwnd, L"Khong co khoa de copy/xuat.");
                } else {
                    copyTextToClipboard(hwnd, key);
                    showInfo(hwnd, L"Da copy khoa vao clipboard.");
                }
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(50, 95, 145));
            return reinterpret_cast<LRESULT>(g_bgBrush ? g_bgBrush : GetStockObject(WHITE_BRUSH));
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(247, 251, 255));
            return reinterpret_cast<LRESULT>(g_panelBrush ? g_panelBrush : GetStockObject(WHITE_BRUSH));
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wParam), &rc, g_bgBrush);
            return 1;
        }
        case WM_DESTROY:
            if (g_titleFont) DeleteObject(g_titleFont);
            if (g_normalFont) DeleteObject(g_normalFont);
            if (g_bgBrush) DeleteObject(g_bgBrush);
            if (g_panelBrush) DeleteObject(g_panelBrush);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int RunGuiApp(HINSTANCE hInstance, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"AesGuiAppWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"AES Encrypt/Decrypt File",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 740,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
