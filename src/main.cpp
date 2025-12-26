#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <atomic>
#include <set>
#include <chrono>
#include <cerrno>
#include <iterator>
#include <csignal>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;
namespace fs = std::filesystem;

struct UserInfo {
    string name;
    string uid;
    string gid;
    string home;
    string shell;
};

static fs::path g_root = "/opt/users";
static atomic<bool> g_running{false};
static thread g_passwd_thread;
static set<string> g_prev_dirs;

static vector<string> g_history;

static const char* kPasswdPath = "/etc/passwd";
static const char* kPasswdReal = "/etc/passwd.real";

static string ltrim(string s) {
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.erase(s.begin());
    return s;
}

static fs::path get_history_path() {
    const char* home = getenv("HOME");
    if (!home || !*home) {
        home = "/root";
    }
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
    try {
        fs::create_directories(p.parent_path());
    } catch (...) {}

    ofstream out(p, ios::app);
    if (out) {
        out << line << '\n';
    }
}

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
    if (!cur.empty()) a.push_back(cur);
    return a;
}

static void write_file(const fs::path& p, const string& content) {
    fs::create_directories(p.parent_path());
    ofstream f(p, ios::trunc);
    if (f) f << content;
}

static bool shell_allows_login(const string& sh) {
    if (sh.size() < 2) return false;
    return sh.rfind("sh") == sh.size() - 2;
}

static vector<UserInfo> read_passwd_real() {
    vector<UserInfo> users;
    const char* path = fs::exists(kPasswdReal) ? kPasswdReal : kPasswdPath;
    ifstream in(path);
    string line;
    while (getline(in, line)) {
        vector<string> f;
        string cur;
        for (char c : line) {
            if (c == ':') { f.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        f.push_back(cur);
        if (f.size() < 7) continue;

        UserInfo u;
        u.name  = f[0];
        u.uid   = f[2];
        u.gid   = f[3];
        u.home  = f[5];
        u.shell = f[6];
        users.push_back(u);
    }
    return users;
}

static bool find_user(const string& name, UserInfo& out) {
    auto users = read_passwd_real();
    for (auto& u : users) {
        if (u.name == name) { out = u; return true; }
    }
    return false;
}

static string next_uid() {
    auto users = read_passwd_real();
    long mx = 2000;
    for (auto& u : users) {
        try {
            long v = stol(u.uid);
            if (v > mx) mx = v;
        } catch (...) {}
    }
    return to_string(mx + 1);
}

static void ensure_tree(const UserInfo& u) {
    fs::create_directories(g_root / u.name);
    write_file(g_root / u.name / "id", u.uid);
    write_file(g_root / u.name / "home", u.home);
    write_file(g_root / u.name / "shell", u.shell);
}

static bool append_passwd_user(const UserInfo& u) {
    ofstream out(kPasswdReal, ios::app);
    if (!out) return false;
    out << u.name << ":x:" << u.uid << ":" << u.gid << "::" << u.home << ":" << u.shell << "\n";
    return true;
}

static void remove_passwd_entry(const string& name) {
    ifstream in(kPasswdReal);
    vector<string> lines;
    string line;
    while (getline(in, line)) {
        if (line.rfind(name + ":", 0) != 0) lines.push_back(line);
    }
    ofstream out(kPasswdReal, ios::trunc);
    for (auto& l : lines) out << l << "\n";
}

static bool run_quiet(const vector<string>& args) {
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        vector<char*> argv;
        for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(1);
    }
    if (pid > 0) {
        int st = 0;
        waitpid(pid, &st, 0);
        return WIFEXITED(st) && WEXITSTATUS(st) == 0;
    }
    return false;
}

static set<string> list_dirs() {
    set<string> dirs;
    std::error_code ec;
    for (auto& p : fs::directory_iterator(g_root, ec)) {
        if (!p.is_directory()) continue;
        dirs.insert(p.path().filename().string());
    }
    return dirs;
}

static void handle_new_dir(const string& name) {
    if (name.empty() || name == "." || name == "..") return;

    UserInfo u;
    if (!find_user(name, u)) {
        run_quiet({"adduser", "--disabled-password", "--gecos", "", "--home", (string("/opt/users/") + name), "--shell", "/bin/sh", name});
        if (!find_user(name, u)) {
            run_quiet({"useradd", "-m", "-d", (string("/opt/users/") + name), "-s", "/bin/sh", name});
            find_user(name, u);
        }
        if (u.name.empty()) {
            u.name = name;
            u.uid = next_uid();
            u.gid = u.uid;
            u.home = "/opt/users/" + name;
            u.shell = "/bin/sh";
            if (!append_passwd_user(u)) return;
        }
    }
    ensure_tree(u);
}

static void handle_removed_dir(const string& name) {
    if (name.empty() || name == "." || name == "..") return;
    UserInfo u;
    if (!find_user(name, u)) return;
    if (run_quiet({"userdel", "-r", name})) return;
    remove_passwd_entry(name);
}

static void process_dir_changes() {
    auto cur = list_dirs();

    for (auto& name : cur) handle_new_dir(name);

    for (auto& oldn : g_prev_dirs) {
        if (cur.find(oldn) == cur.end()) handle_removed_dir(oldn);
    }

    g_prev_dirs = std::move(cur);
}

static void ensure_passwd_real() {
    if (!fs::exists(kPasswdReal)) {
        rename(kPasswdPath, kPasswdReal);
    }

    struct stat st{};
    if (stat(kPasswdPath, &st) == -1 || !S_ISFIFO(st.st_mode)) {
        unlink(kPasswdPath);
        mkfifo(kPasswdPath, 0644);
    }
}

static void write_passwd_snapshot(int fd) {
    process_dir_changes();

    ifstream in(kPasswdReal);
    string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    if (!content.empty()) {
        write(fd, content.data(), content.size());
    }
}

static void passwd_loop() {
    while (g_running.load(std::memory_order_relaxed)) {
        int fd = open(kPasswdPath, O_WRONLY);
        if (fd < 0) {
            if (errno == EINTR) continue;
            this_thread::sleep_for(chrono::milliseconds(1));
            continue;
        }
        write_passwd_snapshot(fd);
        close(fd);
        this_thread::sleep_for(chrono::milliseconds(1));
    }
}

static void start_users_vfs() {
    signal(SIGPIPE, SIG_IGN);

    fs::create_directories(g_root);
    ensure_passwd_real();

    auto users = read_passwd_real();
    for (auto& u : users) {
        if (shell_allows_login(u.shell)) ensure_tree(u);
    }

    g_prev_dirs = list_dirs();
    for (auto& d : g_prev_dirs) handle_new_dir(d);

    g_running = true;
    g_passwd_thread = thread(passwd_loop);
}

static void stop_users_vfs() {
    g_running = false;
    int fd = open(kPasswdPath, O_RDWR | O_NONBLOCK);
    if (g_passwd_thread.joinable()) g_passwd_thread.join();
    if (fd >= 0) close(fd);

    std::error_code ec;
    fs::remove(kPasswdPath, ec);
    fs::copy_file(kPasswdReal, kPasswdPath, fs::copy_options::overwrite_existing, ec);
}

static bool handle_echo_debug(const string& input) {
    string t = ltrim(input);
    if (t.empty()) return false;
    size_t sp = t.find(' ');
    string cmd = (sp == string::npos) ? t : t.substr(0, sp);
    if (cmd != "echo" && cmd != "debug") return false;

    string rest = (sp == string::npos) ? "" : ltrim(t.substr(sp + 1));

    if (rest.size() >= 2) {
        char a = rest.front(), b = rest.back();
        if ((a == '"' && b == '"') || (a == '\'' && b == '\'')) rest = rest.substr(1, rest.size() - 2);
    }

    cout << rest << "\n";
    return true;
}

static bool handle_env(const string& input) {
    string t = ltrim(input);
    if (t.size() < 2) return false;
    if (t[0] != '\\' || t[1] != 'e') return false;

    string rest = ltrim(t.substr(2));
    if (rest.empty() || rest[0] != '$') { cout << "\n"; return true; }

    string var = rest.substr(1);
    size_t sp = var.find_first_of(" \t");
    if (sp != string::npos) var = var.substr(0, sp);

    const char* val_c = getenv(var.c_str());
    string val = val_c ? string(val_c) : "";

    if (var == "PATH") {
        size_t pos = 0;
        while (true) {
            size_t p = val.find(':', pos);
            if (p == string::npos) { cout << val.substr(pos) << "\n"; break; }
            cout << val.substr(pos, p - pos) << "\n";
            pos = p + 1;
        }
        return true;
    }

    cout << val << "\n";
    return true;
}

static void on_sighup(int) {
    cout << "Configuration reloaded\n";
    cout.flush();
}

static void run_external(const vector<string>& args) {
    if (args.empty()) return;

    pid_t pid = fork();
    if (pid == 0) {
        vector<char*> av;
        av.reserve(args.size() + 1);
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

int main() {
    cout << unitbuf;
    cerr << unitbuf;

    signal(SIGHUP, on_sighup);

    load_history();

    start_users_vfs();

    string line;
    while (true) {
        cerr << "$ ";
        if (!getline(cin, line)) break;

        add_to_history(line);

        if (line == "\\q") break;

        if (handle_echo_debug(line)) continue;
        if (handle_env(line)) continue;

        string t = ltrim(line);
        if (t.empty()) continue;

        auto args = split_args(t);
        if (args.empty()) continue;

        run_external(args);
    }

    stop_users_vfs();
    return 0;
}