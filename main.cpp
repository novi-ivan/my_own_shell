#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;
namespace fs = std::filesystem;


static vector<string> g_history;

static string ltrim(string s) {
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t'))
        s.erase(s.begin());
    return s;
}

static fs::path get_history_path() {
    const char* home = getenv("HOME");
    if (!home || !*home) home = "/root";
    return fs::path(home) / ".kubsh_history";
}

static void load_history() {
    g_history.clear();
    fs::path p = get_history_path();
    if (!fs::exists(p)) return;

    ifstream in(p);
    string line;
    while (getline(in, line)) {
        if (!line.empty()) g_history.push_back(line);
    }
}

static void add_to_history(const string& line) {
    string t = ltrim(line);
    if (t.empty()) return;

    g_history.push_back(line);

    fs::path p = get_history_path();
    try { fs::create_directories(p.parent_path()); } catch (...) {}

    ofstream out(p, ios::app);
    if (out) out << line << '\n';
}

// ======================
// Парсинг аргументов
// ======================

static vector<string> split_args(const string& line) {
    vector<string> a;
    string cur;
    bool inq = false;
    char q = 0;

    for (char c : line) {
        if (inq) {
            if (c == q) inq = false;
            else cur += c;
            continue;
        }
        if (c == '"' || c == '\'') {
            inq = true;
            q = c;
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) {
                a.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur += c;
    }
    if (!cur.empty())
        a.push_back(cur);
    return a;
}

// ======================
// echo / debug
// ======================

static bool handle_echo_debug(const string& input) {
    string t = ltrim(input);
    if (t.empty()) return false;

    size_t sp = t.find(' ');
    string cmd = (sp == string::npos ? t : t.substr(0, sp));
    if (cmd != "echo" && cmd != "debug") return false;

    string rest = (sp == string::npos ? "" : ltrim(t.substr(sp + 1)));

    if (rest.size() >= 2) {
        char a = rest.front(), b = rest.back();
        if ((a == '"' && b == '"') || (a == '\'' && b == '\''))
            rest = rest.substr(1, rest.size() - 2);
    }

    cout << rest << "\n";
    return true;
}

// ======================
// \e $VAR
// ======================

static bool handle_env(const string& input) {
    string t = ltrim(input);
    if (t.size() < 2) return false;
    if (t[0] != '\\' || t[1] != 'e') return false;

    string rest = ltrim(t.substr(2));
    if (rest.empty() || rest[0] != '$') {
        cout << "\n";
        return true;
    }

    string var = rest.substr(1);
    size_t sp = var.find_first_of(" \t");
    if (sp != string::npos)
        var = var.substr(0, sp);

    const char* val_c = getenv(var.c_str());
    string val = val_c ? string(val_c) : "";

    if (var == "PATH") {
        size_t pos = 0;
        while (true) {
            size_t p = val.find(':', pos);
            if (p == string::npos) {
                cout << val.substr(pos) << "\n";
                break;
            }
            cout << val.substr(pos, p - pos) << "\n";
            pos = p + 1;
        }
        return true;
    }

    cout << val << "\n";
    return true;
}

// ======================
// SIGHUP
// ======================

static void on_sighup(int) {
    cout << "Configuration reloaded\n";
    cout.flush();
}

// ======================
// Запуск внешних команд
// ======================

static void run_external(const vector<string>& args) {
    if (args.empty()) return;

    pid_t pid = fork();
    if (pid == 0) {
        vector<char*> av;
        for (auto& s : args) av.push_back(const_cast<char*>(s.c_str()));
        av.push_back(nullptr);
        execvp(av[0], av.data());
        _exit(127);
    }

    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 127) {
        cout << args[0] << ": command not found\n";
    }
}

// ======================
// MAIN
// ======================

int main() {
    cout << unitbuf;
    cerr << unitbuf;

    signal(SIGHUP, on_sighup);

    load_history();

    string line;
    while (true) {
        cerr << "$ ";
        if (!getline(cin, line))
            break;

        add_to_history(line);

        if (line == "\\q")
            break;

        if (handle_echo_debug(line))
            continue;

        if (handle_env(line))
            continue;

        string t = ltrim(line);
        if (t.empty())
            continue;

        auto args = split_args(t);
        if (args.empty())
            continue;

        run_external(args);
    }

    return 0;
}