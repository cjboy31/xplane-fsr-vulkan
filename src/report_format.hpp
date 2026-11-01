// SPDX-License-Identifier: MIT
#pragma once
#include <regex>
#include <sstream>
#include <string>
#include <stdexcept>
namespace support_report {
inline constexpr char policy[]="renderer-reports-v1";
inline constexpr size_t maximumLog=240*1024;
inline std::string quote(const std::string& value){
    const char* digits="0123456789abcdef";std::string out="\"";
    for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=digits[c>>4];out+=digits[c&15];}else out+=char(c);}return out+'"';
}
inline std::string redact(const std::string& text){
    static const auto location=std::regex(R"(\b(latitude|longitude|lat|lon|position|model_path|modelpath)\s*[:=])",std::regex::icase);
    static const auto windows=std::regex(R"(([A-Z]:[\\/]|\\\\)[^\r\n"<>]*)",std::regex::icase);
    static const auto home=std::regex(R"(/(home|Users)/[^\r\n"<>]*)");
    static const auto urls=std::regex(R"(https?://[^\s<>"']+)",std::regex::icase);
    static const auto credential=std::regex(R"(\b(authorization|password|secret|api[_-]?key|access[_-]?token|cookie)\s*[:=]\s*[^\r\n]*)",std::regex::icase);
    static const auto email=std::regex(R"([A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,})",std::regex::icase);
    std::istringstream in(text);std::string line,out;
    while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.size()>4096){out+="\n[oversized line omitted]\n";continue;}if(std::regex_search(line,location))continue;
        line=std::regex_replace(line,windows,"[local path removed]");line=std::regex_replace(line,home,"[local path removed]");line=std::regex_replace(line,urls,"[URL removed]");line=std::regex_replace(line,credential,"[credential removed]");line=std::regex_replace(line,email,"[email removed]");if(!out.empty())out+='\n';out+=line;
    }return out;
}
inline std::string boundedLog(const std::string& source){
    if(source.size()<=maximumLog)return redact(source);
    const auto first=source.find('\n',std::min<size_t>(source.size(),512));
    const auto tail=source.find('\n',source.size()-(maximumLog-2048));
    if(first==std::string::npos||tail==std::string::npos)throw std::runtime_error("Renderer log has oversized lines. Use the manual log folder instead.");
    auto result=redact(source.substr(0,first+1)+"[middle of log omitted for upload size]\n"+source.substr(tail+1));
    if(result.size()>maximumLog)throw std::runtime_error("Filtered report is too large");return result;
}
inline bool hexToken(const std::string& s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
inline bool proof(const std::string& s){return s.size()==64&&s.substr(0,4)=="0000";}
inline bool target(const std::string& s){return s=="logs"||s=="bugs";}
inline bool receipt(const std::string& url,const std::string& destination){
    const auto prefix=std::string("https://discord.com/channels/1565487749832777830/")+(destination=="logs"?"1565487905328070878/":"1565779730081255575/");
    return target(destination)&&url.rfind(prefix,0)==0&&url.size()-prefix.size()>=15&&url.size()-prefix.size()<=22&&url.find_first_not_of("0123456789",prefix.size())==std::string::npos;
}
}
