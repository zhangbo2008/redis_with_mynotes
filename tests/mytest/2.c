
#include <stddef.h>

//#include "1.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#define PREFIX_SIZE 1 //dsfaasdfaswhat
#include <string.h>
int main(){
char * s="aaa111111111423423434324232424111";   // char * 本质是一个常量, 他记录的是一个指针,这个指针指向的是内存中专门记录常量的一块地址. debug时候s 的内容是0x400700.非常短.
int aaa=33333333;

char * p="afdsf dsafdsf 111dsaf"; // 现在p的值是0x400722. 看的出来就是上面那个加一段.
char buff[]="afdsf dsafdsf 111dsaf";  // 17,18行同时注释掉.程序报错.
p=buff;  //这样p=buff就对了..........p地址:0x7fffffffe020这个地址怎么这么长了???????? 看看buff地址也是这个0x7fffffffe020.这行代码其实是用p指向了buff.等价于p指向了buff的第一个地址了. 注意p本身就是一个地址. 所以p指向了一个可以修改的地址. char []是可以修改的.所以下面p里面再截字符串之后就等价于修改的是buff里面的数据了!!!!!!!!!!!!!!!整个代码就是从redis上看到的.是非常实用的c语言处理字符串的技巧.
p=strchr(p,' ');
p++;
 printf("%s\n",p);
char *x = strchr(p,' ');
*x = '\0';
printf("%s",p);
char line[1024];
printf("\"junk\" in base-36 --> %ld\n", strtol("1256",NULL,10));
printf("\"junk\" in base-36 --> %ld\n", strtol("a",NULL,36));
printf("\"junk\" in base-36 --> %ld\n", strtol("b",NULL,36));
printf("\"junk\" in base-36 --> %ld\n", strtol("ab",NULL,36));
printf("\"junk\" in base-36 --> %ld\n", strtol("aa",NULL,36));
printf("%d",sizeof(line));
int a3=4,b;
printf("%d,%d\n",a3,b);
char * a=malloc(2);
a[0]='a';
int p2=*a;
printf("%d",p2);
int aaaa;
}
