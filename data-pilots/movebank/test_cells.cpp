// Test the exact parser cells before any prospective normalization.
#define main preflightCommandMain
#include "preflight.cpp"
#undef main

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    const auto events = readCsv(argv[1]);
    const auto package = readCsv(argv[2]);
    const auto check = [](bool value) { if (!value) throw std::runtime_error("exact_cell_regression"); };
    check(events.size() == 3);
    check(events[1][0] == "synthetic-001");
    check(events[1][1] == "2020-02-29 12:34:56.007");
    check(events[1][2] == "174.000001");
    check(events[1][4] == "Tūī, \"one\"");
    check(events[1][5] == "tag-synthetic-1");
    check(events[1][10].empty());
    check(package[1][3] == "Synthetic author, \"fixture only\"\nNot a published dataset");
    std::cout << "Exact CSV cells preserved; no normalized IDs or timestamps generated.\n";
}
