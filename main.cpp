#include <iostream>
#include <string>
#include <fstream>
using namespace std;

static string history_path() {
    const char* home = getenv("HOME");
    string base = home ? string(home) : "";
    if (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/.kubsh_history";
}
static void append_history(const string& line) {
    ofstream out(history_path(), ios::app);
    if (out) out << line << '\n';
}
void print_history() {
    string line;
    ifstream in(history_path());
    string last_command;
    if (!in) {
        cout << "История пуста" << endl;
        return;
    }
    cout<<"Your command history:";
    while(getline(in,line))
        cout<<line<<'\n';
}
int main() {
    cout << unitbuf;
    cerr << unitbuf;
    while(true){
        string input;
        getline(std::cin, input);
        if (input.empty()) continue;
        append_history(input);
        if (input=="/q") return 0;
        if (input == "history") {
            print_history();
            continue;
        }
        cout << input << ": command not found" << endl;
    }
}

