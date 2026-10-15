#include <fstream>
extern int fixture_value();
int main()
{
    std::ofstream marker("test-executed.txt");
    marker << fixture_value();
    return marker && fixture_value() == 42 ? 0 : 1;
}
