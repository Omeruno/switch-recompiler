#include "source/runtime/command_line.h"
#include <cassert>
#include <cstdio>
int main() {
    const std::vector<std::string> args={"S:\\Apps\\Public app.exe","", "a b", "a\tb", "plain", "a\"b", "slash\\", "\\\\\"quoted", "文件 😀", "&|<>%x%"};
    std::string line;std::vector<std::string> decoded;
    assert(rt::BuildCommandLine(args,line));assert(rt::ParseCommandLine(line,decoded,true) && decoded==args);
    assert(rt::ParseCommandLine(std::string("app.exe a\"\"b \"x\"\"y\""),decoded,true));
    assert(decoded==std::vector<std::string>({"app.exe","ab","x\"y"}));
    assert(!rt::ParseCommandLine(std::string("app.exe \"unterminated"),decoded,true) && decoded.empty());
    assert(rt::ParseCommandLine(std::string("app.exe \"unterminated"),decoded,false) && decoded[1]=="unterminated");
    assert(!rt::ParseCommandLine(std::string(rt::MaxCommandLine+1,'a'),decoded));
    assert(!rt::BuildCommandLine(std::vector<std::string>(rt::MaxCommandArgs+1,"a"),line) && line.empty());
    assert(!rt::BuildCommandLine({std::string("bad\0data",8)},line));
    assert(!rt::ParseCommandLine(std::string("bad\0data",8),decoded));
    // All slash counts around quotes/terminal slashes, including empty arguments.
    for(int n=0;n<32;++n)for(const auto& tail:{"", "\"", " x", "\\"}) {
        const std::vector<std::string> sample={"app.exe",std::string(n,'\\')+tail};
        assert(rt::BuildCommandLine(sample,line));assert(rt::ParseCommandLine(line,decoded,true) && decoded==sample);
    }
    puts("Windows argv: bounded quote/backslash/empty/Unicode round trips and malformed input PASS");
}
