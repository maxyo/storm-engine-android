#pragma once
#include <map>
#include <string>
#include <cctype>
struct ATTRIBUTES {
 std::string value;std::map<std::string,ATTRIBUTES> fields;
 static std::string key(std::string s){for(auto &c:s)c=std::tolower(c);return s;}
 ATTRIBUTES *GetAttributeClass(std::string n){auto it=fields.find(key(n));return it==fields.end()?nullptr:&it->second;}
 const char *GetAttribute(const char *n){auto *a=GetAttributeClass(n);return a?a->value.c_str():nullptr;}
 const char *GetAttribute(unsigned n){if(n>=fields.size())return nullptr;auto i=fields.begin();std::advance(i,n);return i->second.value.c_str();}
 unsigned GetAttributesNum(){return fields.size();}const char *GetThisAttr(){return value.c_str();}
 ATTRIBUTES *FindAClass(ATTRIBUTES *root,const char *p){std::string s=p;auto pos=s.find('.');if(!root)return nullptr;auto *a=root->GetAttributeClass(s.substr(0,pos));return pos==s.npos?a:FindAClass(a,s.substr(pos+1).c_str());}
 void set(std::string path,std::string text){auto p=path.find('.');if(p==path.npos)fields[key(path)].value=text;else fields[key(path.substr(0,p))].set(path.substr(p+1),text);}
};
