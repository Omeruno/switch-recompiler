#pragma once
#include <string>
#include <vector>

namespace rt {
constexpr size_t MaxCommandLine=16384,MaxCommandArgs=64;
// Bounded Windows CRT quote/backslash rules, no shell expansion. Strict mode
// rejects unmatched quotes for interactive Run; CRT accepts the final quoted tail.
template<class Char> bool ParseCommandLine(const std::basic_string<Char>& text,
    std::vector<std::basic_string<Char>>& out,bool strict=false) {
    out.clear();
    if(text.size()>MaxCommandLine || text.find(Char(0))!=text.npos)return false;
    size_t i=0;
    while(i<text.size()) {
        while(i<text.size() && (text[i]==Char(' ') || text[i]==Char('\t')))++i;
        if(i==text.size())break;
        if(out.size()==MaxCommandArgs){out.clear();return false;}
        std::basic_string<Char> arg;bool quoted=false;
        while(i<text.size() && (quoted || (text[i]!=Char(' ') && text[i]!=Char('\t')))) {
            size_t slashes=0;
            while(i<text.size() && text[i]==Char('\\')){++slashes;++i;}
            if(i<text.size() && text[i]==Char('"')) {
                arg.append(slashes/2,Char('\\'));
                if(slashes%2){arg.push_back(Char('"'));++i;}
                else if(quoted && i+1<text.size() && text[i+1]==Char('"')){arg.push_back(Char('"'));i+=2;}
                else {quoted=!quoted;++i;}
            } else {
                arg.append(slashes,Char('\\'));
                if(i<text.size() && (quoted || (text[i]!=Char(' ') && text[i]!=Char('\t'))))arg.push_back(text[i++]);
            }
        }
        if(strict && quoted){out.clear();return false;}
        out.push_back(std::move(arg));
    }
    return true;
}
inline std::string QuoteCommandArgument(const std::string& arg) {
    // Always quote: this also encodes empty args and terminal backslashes.
    std::string out="\"";size_t slashes=0;
    for(char c:arg) {
        if(c=='\\'){++slashes;continue;}
        out.append(c=='"'?slashes*2+1:slashes,'\\');slashes=0;out.push_back(c);
    }
    out.append(slashes*2,'\\');out+='"';return out;
}
inline bool BuildCommandLine(const std::vector<std::string>& args,std::string& out) {
    out.clear();if(args.size()>MaxCommandArgs)return false;
    for(const auto& arg:args) {
        if(arg.size()>MaxCommandLine || arg.find('\0')!=arg.npos){out.clear();return false;}
        const auto encoded=QuoteCommandArgument(arg);
        if(out.size()+encoded.size()+(!out.empty()?1:0)>MaxCommandLine){out.clear();return false;}
        if(!out.empty())out+=' ';
        out+=encoded;
    }
    return true;
}
} // namespace rt
