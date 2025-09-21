#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
using namespace std;

string history_path() {
    const char* home = getenv("HOME");
    string base = home ? string(home) : "";
    if (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/.kubsh_history";
}

void append_history(const string& line) {
    ofstream out(history_path(), ios::app);
    if (out) out << line << '\n';
}

void print_history() {
    ifstream in(history_path());
    string line;
    cout << "Your command history:\n";
    while (getline(in, line)) cout << line << '\n';
}

vector<string> split(const string& line) {
    vector<string> tokens;
    istringstream iss(line);
    string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

bool cmd_exit(const vector<string>&) {
    return true;
}

bool cmd_history(const vector<string>&) {
    print_history();
    return false;
}

bool cmd_echo(const vector<string>& args) {
    for (size_t i = 1; i < args.size(); i++) {
        string word = args[i];
        if (i > 1) cout << " ";
        cout << word;
    }
    cout << "\n";
    return false;
}

bool cmd_unknown(const vector<string>& args) {
    cout << args[0] << ": command not found\n";
    return false;
}

bool run_command(const vector<string>& args) {
    if (args.empty()) return false;
    if (args[0] == "/q")      return cmd_exit(args);
    if (args[0] == "history") return cmd_history(args);
    if (args[0] == "echo")    return cmd_echo(args);
    return cmd_unknown(args);
}

int main() {
    cout << unitbuf;
    cerr << unitbuf;

    while (true) {
        string input;
        if (!getline(cin, input)) { cout << "\n"; break; } // Ctrl+D
        if (input.empty())
            continue;
        append_history(input);
        auto args = split(input);
        if (run_command(args))
            break;
    }
    return 0;
}