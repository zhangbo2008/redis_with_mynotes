/* zmalloc - total amount of allocated memory aware version of malloc()
 *
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>

/* This function provide us access to the original libc free(). This is useful
 * for instance to free results obtained by backtrace_symbols(). We need
 * to define this function before including zmalloc.h that may shadow the
 * free implementation if we use jemalloc or another non standard allocator. */
//标准的free函数
void zlibc_free(void *ptr) {
    free(ptr);
}

#include <string.h>
#include <pthread.h>
#include "config.h"
#include "zmalloc.h"

#ifdef HAVE_MALLOC_SIZE
#define PREFIX_SIZE (0)// 如果已经有malloc_size函数,那么我们就不用prefix变量了.直接干成0.保证代码通用
#else
#if defined(__sun) || defined(__sparc) || defined(__sparc__)//这些系统上是long long
#define PREFIX_SIZE (sizeof(long long))
#else
#define PREFIX_SIZE (sizeof(size_t)) //其他系统是size_t, 我这个系统上long long 跟long 都是8字节.
#endif
#endif








/* Explicitly override malloc/free etc when using tcmalloc. */
//如果用tcmalloc ,那么就函数名宏替换即可.
#if defined(USE_TCMALLOC)
#define malloc(size) tc_malloc(size)
#define calloc(count,size) tc_calloc(count,size)
#define realloc(ptr,size) tc_realloc(ptr,size)
#define free(ptr) tc_free(ptr)
#elif defined(USE_JEMALLOC)
#define malloc(size) je_malloc(size)
#define calloc(count,size) je_calloc(count,size)
#define realloc(ptr,size) je_realloc(ptr,size)
#define free(ptr) je_free(ptr)
#endif










#ifdef HAVE_ATOMIC//并发需要原子操作.这里面是先加再范湖这个数.
#define update_zmalloc_stat_add(__n) __sync_add_and_fetch(&used_memory, (__n))//这个函数第一个传地址,第二个传int
#define update_zmalloc_stat_sub(__n) __sync_sub_and_fetch(&used_memory, (__n))
#else    

/* 
如果没有atomic那么我们就只能线程枷锁了.这个方法会耽误效能了.还是用宏来写,提高运行速度,下面使用锁used_memory_mutex,


记住宏函数都用do while 0套起来.
 */ // malloc  stl
#define update_zmalloc_stat_add(__n) do { \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory += (__n); \
    pthread_mutex_unlock(&used_memory_mutex); \
} while(0)


#define update_zmalloc_stat_sub(__n) do { \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory -= (__n); \
    pthread_mutex_unlock(&used_memory_mutex); \
} while(0)
#endif















// _作为变量或者函数的开始字符,表示内部函数. private.
//宏要写到一行.// 记录内存使用加了n个. 并且内部不能加注释. 注意这个函数如果你输入的_n正好是8的倍数,那么这个函数就不修改_n,否则补齐到下一个8的倍数.
#define update_zmalloc_stat_alloc(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_add(_n); \
    } else { \
        used_memory += _n; \
    } \
} while(0)

#define update_zmalloc_stat_free(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_sub(_n); \
    } else { \
        used_memory -= _n; \
    } \
} while(0)







//设置一个static来记录使用内存. 这里面static是必须写的吗?  在定义不需要与其他文件共享的全局变量时，加上static关键字能够有效地降低程序模块之间的耦合，避免不同文件同名变量的冲突，且不会误使用。 必须使用. 每次用的时候都避免冲突. 基本上函数外面的变量都加上static!!!!!!!!!!!!!!!!!!!!
static size_t used_memory = 0;
static int zmalloc_thread_safe = 0;// 默认不开启线程安全保证安全. 这样提高性能.
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;//来个互斥锁,这个锁保证同一时刻，只能有一个线程持有该锁。所以很安全.










static void zmalloc_default_oom(size_t size) {//打印一个错误语句
    fprintf(stderr, "zmalloc: Out of memory trying to allocate %zu bytes\n",
        size);
    fflush(stderr);//马上刷出来,让用户看到
    abort();// 退出程序,所以调用他的函数也直接强制停了.
}






// 因为函数本身也是一个指针, 所以只能用一个函数指针来做等号. 所以左边定义的里面也必须写一个*表示一个地址.
static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

void *zmalloc(size_t size) {
    void *ptr = malloc(size+PREFIX_SIZE);// PREFIX_SIZE =8

    if (!ptr) zmalloc_oom_handler(size);//out of memory处理方法 ;表示if已经都跑完了判断了结果.
//下面接一个宏. 大量使用宏可以提高代码性能.少占用运行时间.多用编译时间来换.
#ifdef HAVE_MALLOC_SIZE
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    return ptr;
#else//我的centos没有这个函数,所以直接进入else逻辑.
    *((size_t*)ptr) = size;//前8个写入size大小.  指针写入方法: 先转化指针,等价于告诉他,写入部分占用的空间.void * 必须先转化才能用.  size_t 就是int  , 足够放,因为 64位机, 也就是指针一共有 2**64 这么多个.
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);  //PREFIX_SIZE 是用来写入size信息的.
    return (char*)ptr+PREFIX_SIZE; //前面转化为char * ,所以后面加8就等价于地址加8
#endif


}










void *zcalloc(size_t size) {
    void *ptr = calloc(1, size+PREFIX_SIZE); //申请一个后面这么大的东西

    if (!ptr) zmalloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    return ptr;
#else
    *((size_t*)ptr) = size;
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    return (char*)ptr+PREFIX_SIZE;//其他代码跟zmalloc一样.
#endif
}









void *zrealloc(void *ptr, size_t size) {
    //re , allocate 函数
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
#endif
    size_t oldsize;
    void *newptr;

    if (ptr == NULL) return zmalloc(size);




#ifdef HAVE_MALLOC_SIZE
    oldsize = zmalloc_size(ptr); //之前分配的大小.
    newptr = realloc(ptr,size);  //调用系统函数把ptr大小变到size.然后返回新指针.
    if (!newptr) zmalloc_oom_handler(size);

    update_zmalloc_stat_free(oldsize);//更新内存记录的数据
    update_zmalloc_stat_alloc(zmalloc_size(newptr));
    return newptr;
#else  //如果没有malloc_size函数,我们就手动来计算oldsize
    realptr = (char*)ptr-PREFIX_SIZE; //这一段要跟zmalloc函数对比来看. zmalloc当 没有HAVE_MALLOC_SIZE的情况下, 返回的地址是带偏移量的所以我们这个地方要-来找到真实地址.
    oldsize = *((size_t*)realptr); //下面就都类似了.
    newptr = realloc(realptr,size+PREFIX_SIZE);
    if (!newptr) zmalloc_oom_handler(size);

    *((size_t*)newptr) = size;  //写入新size
    update_zmalloc_stat_free(oldsize);
    update_zmalloc_stat_alloc(size);
    return (char*)newptr+PREFIX_SIZE;
#endif
}













/* Provide zmalloc_size() for systems where this function is not provided by
 * malloc itself, given that in that case we store a header with this
 * information as the first bytes of every allocation. */// 如果缺这个函数,我们就补上这个函数的实现.
#ifndef HAVE_MALLOC_SIZE 
size_t zmalloc_size(void *ptr) {
    void *realptr = (char*)ptr-PREFIX_SIZE;  //因为每一次传入地址的前面8个字节用来纯size了.
    size_t size = *((size_t*)realptr); //抽取这个size数据.
    /* Assume at least that all the allocations are padded at sizeof(long) by
     * the underlying allocator. */ //使用logn来做prefiex
    if (size&(sizeof(long)-1)) size += sizeof(long)-(size&(sizeof(long)-1)); //malloc 底层开这么大.
    return size+PREFIX_SIZE;   //PREFIX_SIZE 是我们自己开的.
}//倒数第二行, if 里面是size&7 表示size最后3位如果有1就进入逻辑. 把size补到最后3位都是0的. size至少是8.这个补齐是因为malloc函数本身就会进行最后3个位的补齐.所以这里面这样算可以精准算malloc函数开了多大内存.
#endif








void zfree(void *ptr) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
    size_t oldsize;
#endif

    if (ptr == NULL) return;
#ifdef HAVE_MALLOC_SIZE
    update_zmalloc_stat_free(zmalloc_size(ptr));
    free(ptr);
#else
    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);
    update_zmalloc_stat_free(oldsize+PREFIX_SIZE);
    free(realptr);
#endif
}










char *zstrdup(const char *s) {
    size_t l = strlen(s)+1; //加上最后的/0才是字符串真正占用的空间.
    char *p = zmalloc(l);

    memcpy(p,s,l);      //从s复制l个到p
    return p;
}






size_t zmalloc_used_memory(void) {
    size_t um;//初始化一个变量.

    if (zmalloc_thread_safe) {
#ifdef HAVE_ATOMIC
        um = __sync_add_and_fetch(&used_memory, 0);
#else
        pthread_mutex_lock(&used_memory_mutex);
        um = used_memory;
        pthread_mutex_unlock(&used_memory_mutex);
#endif
    }
    else {
        um = used_memory; //让公共加锁的变量,传出来,然后他就可以继续干其他的了,提高了性能,//不要直接传传出来use_memory.
    }

    return um;
}









void zmalloc_enable_thread_safeness(void) {
    zmalloc_thread_safe = 1;
}





//修改oom处理函数.
void zmalloc_set_oom_handler(void (*oom_handler)(size_t)) {
    zmalloc_oom_handler = oom_handler;
}

//下面不推荐看了. 只是简单讲一下.

/* Get the RSS information in an OS-specific way.
 *
 * WARNING: the function zmalloc_get_rss() is not designed to be fast
 * and may not be called in the busy loops where Redis tries to release
 * memory expiring or swapping out objects.
 *
 * For this kind of "fast RSS reporting" usages use instead the
 * function RedisEstimateRSS() that is a much faster (and less precise)
 * version of the function. */
//看这个变量是否之前定义了.
#if defined(HAVE_PROC_STAT)   
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//rss是Resident Set Size的缩写，表示该进程所占物理内存的大小，是操作系统分配给Redis实例的内存大小
size_t zmalloc_get_rss(void) {
    int page = sysconf(_SC_PAGESIZE);// 获取分页大小
    size_t rss;
    char buf[4096];
    char filename[256];
    int fd, count;
    char *p, *x;

    snprintf(filename,256,"/proc/%d/stat",getpid()); //从getpid()读取格式%d,放入filename里面
    if ((fd = open(filename,O_RDONLY)) == -1) return 0;//开不开就return 0
    if (read(fd,buf,4096) <= 0) {   //读文件失败也返回0陈宫了,就放入buf中了.
        close(fd);
        return 0;
    }
    close(fd);

    p = buf;//下面玩buf变量.
    count = 23; /* RSS is the 24th field in /proc/<pid>/stat */
    while(p && count--) {
        p = strchr(p,' ');  // strchr str中找一个char的索引这里面就是p中找到' '的指针.
        if (p) p++;// //然后加1 所以count=1的时候我们while循环一次.找到第二个field
        //所以我们count=23,while循环23次,找到第24个field.
    }
    if (!p) return 0;//找不到就返回0, 找不到的时候p返回的是null指针.
    x = strchr(p,' ');
    if (!x) return 0;//再找一个域
    *x = '\0';//然后指针写入\0,这样p就是截取了这个字符串了.

    rss = strtoll(p,NULL,10);//字符串按照10进制得到数字.
    rss *= page; //乘以页数. rss是页数, page是一页多大,一般是4kb
    return rss;
}
#elif defined(HAVE_TASKINFO) //苹果系统的
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/task.h>
#include <mach/mach_init.h>

size_t zmalloc_get_rss(void) {//这里面的函数我们都不用管了,苹果的linux点不进去,看不到函数细节.
    task_t task = MACH_PORT_NULL;
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;

    if (task_for_pid(current_task(), getpid(), &task) != KERN_SUCCESS)
        return 0;
    task_info(task, TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);

    return t_info.resident_size;
}
#else
size_t zmalloc_get_rss(void) {//如果os没有对应的.那么才使用我们自己这个.c里面计算的结果.
    /* If we can't get the RSS in an OS-specific way for this system just
     * return the memory usage we estimated in zmalloc()..
     *
     * Fragmentation will appear to be always 1 (no fragmentation)
     * of course... */
    return zmalloc_used_memory();
}
#endif











//获取使用的比例, rss是系统分配的, zmalloc_used_memory()是我们实际使用的.
/* Fragmentation = RSS / allocated-bytes */
float zmalloc_get_fragmentation_ratio(size_t rss) {
    return (float)rss/zmalloc_used_memory();//先转化float,不然是int除法.
}



//linux里面定义了这个smaps
#if defined(HAVE_PROC_SMAPS)
size_t zmalloc_get_private_dirty(void) {
    char line[1024];
    size_t pd = 0;
    FILE *fp = fopen("/proc/self/smaps","r");

    if (!fp) return 0;
    while(fgets(line,sizeof(line),fp) != NULL) {
        if (strncmp(line,"Private_Dirty:",14) == 0) {
            char *p = strchr(line,'k');//如果匹配到了14,找到k字母
            if (p) {//如果找到k字母//因为p指向的本质是 char [],所以可以直接修改p指针的指向.
                *p = '\0';//直接修改指向.
                pd += strtol(line+14,NULL,10) * 1024;//把大小加到记录值中.
            }
        }
    }
    fclose(fp);
    return pd;
}
#else
size_t zmalloc_get_private_dirty(void) {
    return 0;
}
#endif
