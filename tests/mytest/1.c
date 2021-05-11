//疯狂的贴zmalloc.c里面的函数,可算可以跑了. 然后配置debug方法, 首先以为是单文件,所以用gcc -g 编译掉,之后我们修改launch.json里面program为:     "program": "${workspaceFolder}/tests/mytest/a.out"  就可以跑了!!!!!!!!!!!!!所以vscode调试的原理就是 你先用gcc -g 生成了带调试信息的编译后的文件a.out 然后源码上打断点, 开F5, vscode就会debug模式启动了. 他自己会根据调试信息找到对应的代码所在位置.//我们跟踪一遍,看看这个malloc_size函数咋玩的.
//这个脚本只能玩到这里了.因为malloc函数底层实现想看的话需要进入linux源码debug. 现在还没有源码.等以后研究内核再看malloc细节吧.
#include <stddef.h>

//#include "1.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>
#include <pthread.h>

#define PREFIX_SIZE 0
#define HAVE_MALLOC_SIZE 

static size_t used_memory = 0;
#define update_zmalloc_stat_add(__n) __sync_add_and_fetch(&used_memory, (__n))//这个函数第一个传地址,第二个传int
#define update_zmalloc_stat_sub(__n) __sync_sub_and_fetch(&used_memory, (__n))


//就是下面这个函数看不懂!!!!!!!!!!!!!!!20行来个断点.
size_t zmalloc_size(void *ptr) {
    void *realptr = (char*)ptr-PREFIX_SIZE;  //因为每一次传入地址的前面8个字节用来纯size了.
    size_t size = *((size_t*)realptr); //抽取这个size数据.
    /* Assume at least that all the allocations are padded at sizeof(long) by
     * the underlying allocator. */ //使用logn来做prefiex
    if (size&(sizeof(long)-1)) size += sizeof(long)-(size&(sizeof(long)-1));
    return size+PREFIX_SIZE;
}



//这个逻辑是, 如果来的_n, 后3位有数据就补齐.然后内存加上这个补齐后的数据.

#define update_zmalloc_stat_alloc(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (1) { \
        update_zmalloc_stat_add(_n); \ 
    } else { \
        used_memory += _n; \
    } \
} while(0);



static void zmalloc_default_oom(size_t size) {//打印一个错误语句
    fprintf(stderr, "zmalloc: Out of memory trying to allocate %zu bytes\n",
        size);
    fflush(stderr);
    abort();// 退出程序,所以调用他的函数也直接强制停了.
}

static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

void *zmalloc(size_t size) {
    void *ptr = malloc(size+PREFIX_SIZE);// PREFIX_SIZE =8

    if (!ptr) zmalloc_oom_handler(size);//out of memory处理方法 ;表示if已经都跑完了判断了结果.
//下面接一个宏. 大量使用宏可以提高代码性能.少占用运行时间.多用编译时间来换.
#ifdef HAVE_MALLOC_SIZE
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    return ptr;//懂了!!!!!!!!!!!!!!!!

    /*
    这部分逻辑是这样的.
    HAVE_MALLOC_SIZE 表示的是os里面的malloc函数会自动在申请内存的前面size_t的字节中记录好后面这个单元的内存片开的长度.所以我们就在zmalloc_size代码中先-这个size_t,然后找到这个长度值.实际上这个长度值少了补齐运算,我们也补齐上. 就是这个部分if (size&(sizeof(long)-1)) size += sizeof(long)-(size&(sizeof(long)-1)); 最后再加上我们redis需要多开prefix个字节.所以内存用了这么大size+PREFIX_SIZE; 因为prefix是8.所以我们size先补齐到8的倍数再加prefix跟先加prefix再补齐是一样的结果.

    然后进入update_zmalloc_stat_alloc 这个函数.把这个8的倍数加到use_memory变量中.
    整函数update_zmalloc_stat_alloc如果输入8的倍数就不修改,如果不到8的倍数就自动对齐到下个8的倍数


    
    
    
    
    
    
    
    
    */
#else //如果os里面没有malloc_size函数.那么其实malloc里面真正放入的是先放size再放数据.
//所以我们直接从ptr上取size数据,一样补齐后,写入全局变量memory中记录内存使用情况.
    *((size_t*)ptr) = size;//前8个写入size大小. ,更新ptr里面的数据.
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
//所以找到size之后我们用update_zmalloc_stat_alloc对齐到8的倍数.
//所以真正的存data的地方是prt+prefix_size
//(char*)ptr+PREFIX_SIZE; 这句话前面这个char* 就是为了后面的+8,每一次走一个字节.总共走8次.

    return (char*)ptr+PREFIX_SIZE;
#endif
}
int main(){
void * a=zmalloc(8);

    printf("dsafdsf\n");
  printf("%d\n",sizeof(size_t)); // 显示8
//看看long在地址里面站多少
long a2[]={1,2,3};
printf("%p\n",a2);//地址xxx300
printf("%p\n",a2+1);//地址xxx308
// 8
// 0x7ffcdc573d10
// 0x7ffcdc573d18
}
