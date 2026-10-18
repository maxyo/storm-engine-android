#include "storm_archive.h"
#include <cstdio>
int main(int argc,char **argv) {
 char error[2048]{},format[64]{};
 if(argc<2||argc>3)return 64;
 int result=argc==2?storm_archive_inspect(argv[1],format,sizeof(format),error,sizeof(error)):
  storm_archive_extract(argv[1],argv[2],nullptr,nullptr,nullptr,error,sizeof(error));
 printf("%d %s %s\n",result,format,error);return result;
}
