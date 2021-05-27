 #include <stdio.h>
 #include <sys/time.h>
 #include <sys/types.h>
 #include <unistd.h>
 #include <stdlib.h>
 
 #include "ae.h"
 #include "zmalloc.h"
 #include "config.h"
 
 /* Include the best multiplexing layer supported by this system.
  * The following should be ordered by performances, descending. */    //为了支持不同的平，redis用相同的接口封装了系统提供的多路复用层代码。接口共提供如下函数：ApiCreate\aeApiFree\aeApiAddEvent\aeApiDelEvent\aeApiPoll\aeApiName函数，以及一个struct ApiState    //通过包含不同的头文件，选择不同的底层实现    //按如下顺序，效率递减： epoll > ueue > select
 #ifdef HAVE_EPOLL
 #include "ae_epoll.c"
 #else
     #ifdef HAVE_KQUEUE
     #include "ae_kqueue.c"
     #else
     #include "ae_select.c"
     #endif
 #endif
     //初始化函数，创建事件循环，函数内部alloc一个结构，用于表示事件状态，供后续其他函数作为参数使









 aeEventLoop *aeCreateEventLoop(void) {
     aeEventLoop *eventLoop;
     int i;
 
     eventLoop = zmalloc(sizeof(*eventLoop));
     if (!eventLoop) return NULL;        //时间event用链表存储
     eventLoop->timeEventHead = NULL;
     eventLoop->timeEventNextId = 0;        //表示是否停止事件循环
     eventLoop->stop = 0;        //maxfd只由ae_select.c使用，后续有些相关的处理，如果使用epoll话，其实可以进行简化
     eventLoop->maxfd = -1;        //每次调用epoll\select前调用的函数，由框架使用者注册
     eventLoop->beforesleep = NULL;
     if (aeApiCreate(eventLoop) == -1) {
         zfree(eventLoop);
         return NULL;
     }
     /* Events with mask == AE_NONE are not set. So let's initialize the
      * vector with it. */                //将所有的文件描述符的mask设置为无效值，作为初始化
     for (i = 0; i < AE_SETSIZE; i++)
         eventLoop->events[i].mask = AE_NONE;
     return eventLoop;
 }










     //底层实现执行释放操作后，释放state的内存
 void aeDeleteEventLoop(aeEventLoop *eventLoop) {
     aeApiFree(eventLoop);
     zfree(eventLoop);
 }
     //停止事件循环,redis作为一个无限循环的server，在redis.c中并没有任何一处调用此函数
 void aeStop(aeEventLoop *eventLoop) {
     eventLoop->stop = 1;
 }







     //创建文件fd事件，加入事件循环监控列表，使得后续epoll\select时将会测试这个文件描述符的可读性可写性）
 int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
         aeFileProc *proc, void *clientData)
 {
     if (fd >= AE_SETSIZE) return AE_ERR;
     aeFileEvent *fe = &eventLoop->events[fd];
 
     if (aeApiAddEvent(eventLoop, fd, mask) == -1)
         return AE_ERR;
     fe->mask |= mask;        //注册函数
     if (mask & AE_READABLE) fe->rfileProc = proc;
     if (mask & AE_WRITABLE) fe->wfileProc = proc;
     fe->clientData = clientData;        //更新maxfd
     if (fd > eventLoop->maxfd)
         eventLoop->maxfd = fd;
     return AE_OK;
 }








 
 
 void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
 {
     if (fd >= AE_SETSIZE) return;
     aeFileEvent *fe = &eventLoop->events[fd];
 
     if (fe->mask == AE_NONE) return;
     fe->mask = fe->mask & (~mask);
     if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
         /* Update the max fd */
         int j;
 
         for (j = eventLoop->maxfd-1; j >= 0; j--)
             if (eventLoop->events[j].mask != AE_NONE) break;
         eventLoop->maxfd = j;
     }
     aeApiDelEvent(eventLoop, fd, mask);
 }
     //返回值为该文件描述符关注的事件类型（可读、可写）
 int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {
     if (fd >= AE_SETSIZE) return 0;
     aeFileEvent *fe = &eventLoop->events[fd];
 
     return fe->mask;
 }
 
 static void aeGetTime(long *seconds, long *milliseconds)
 {
     struct timeval tv;
 
     gettimeofday(&tv, NULL);
     *seconds = tv.tv_sec;
     *milliseconds = tv.tv_usec/1000;
 }
 
 static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
     long cur_sec, cur_ms, when_sec, when_ms;
 
     aeGetTime(&cur_sec, &cur_ms);
     when_sec = cur_sec + milliseconds/1000;
     when_ms = cur_ms + milliseconds%1000;
     if (when_ms >= 1000) {
         when_sec ++;
         when_ms -= 1000;
     }
     *sec = when_sec;
     *ms = when_ms;
 }
 
 long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
         aeTimeProc *proc, void *clientData,
         aeEventFinalizerProc *finalizerProc)
 {
     long long id = eventLoop->timeEventNextId++;
     aeTimeEvent *te;
 
     te = zmalloc(sizeof(*te));
     if (te == NULL) return AE_ERR;
     te->id = id;        //time event执行时间为绝对时间点
     aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
     te->timeProc = proc;
     te->finalizerProc = finalizerProc;
     te->clientData = clientData;        //time event链表为无序表，直接插入到链表头
     te->next = eventLoop->timeEventHead;
     eventLoop->timeEventHead = te;
     return id;
 }
     //从链表中删除，是以id作为key查找的（顺序遍历）
 int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
 {
     aeTimeEvent *te, *prev = NULL;
 
     te = eventLoop->timeEventHead;
     while(te) {
         if (te->id == id) {
             if (prev == NULL)
                 eventLoop->timeEventHead = te->next;
             else
                 prev->next = te->next;
             if (te->finalizerProc)
                 te->finalizerProc(eventLoop, te->clientData);
             zfree(te);
             return AE_OK;
         }
         prev = te;
         te = te->next;
     }
     return AE_ERR; /* NO event with the specified ID found */
 }
 
 /* Search the first timer to fire.
  * This operation is useful to know how many time the select can be
  * put in sleep without to delay any event. （没大看懂）,也许是给一个阻塞时间的上限值
  * If there are no timers NULL is returned.
  *
  * Note that's O(N) since time events are unsorted.
  * Possible optimizations (not needed by Redis so far, but...):
  * 1) Insert the event in order, so that the nearest is just the head.
  *    Much better but still insertion or deletion of timers is O(N).
  * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
  */
 static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop)
 {
     aeTimeEvent *te = eventLoop->timeEventHead;
     aeTimeEvent *nearest = NULL;
 
     while(te) {
         if (!nearest || te->when_sec < nearest->when_sec ||
                 (te->when_sec == nearest->when_sec &&
                  te->when_ms < nearest->when_ms))
             nearest = te;
         te = te->next;
     }
     return nearest;
 }
     //对time event进行处理
 /* Process time events */
 static int processTimeEvents(aeEventLoop *eventLoop) {
     //处理的事件数188     int processed = 0;
     aeTimeEvent *te;
     long long maxId;
 
     te = eventLoop->timeEventHead;
     maxId = eventLoop->timeEventNextId-1;
     while(te) {
         long now_sec, now_ms;
         long long id;
 
         if (te->id > maxId) {
             te = te->next;
             continue;
         }
         aeGetTime(&now_sec, &now_ms);
         if (now_sec > te->when_sec ||
             (now_sec == te->when_sec && now_ms >= te->when_ms))
         {
             int retval;
 
             id = te->id;                //如果执行时间到或已超出，则执行对应的时间处理函数
             retval = te->timeProc(eventLoop, id, te->clientData);
             processed++;
             /* After an event is processed our time event list may
              * no longer be the same, so we restart from head. //因为时间处理函数timeProc可改变此链表
              * Still we make sure to don't process events registered
              * by event handlers itself in order to don't loop forever.？
              * To do so we saved the max ID we want to handle.
              *
              * FUTURE OPTIMIZATIONS:
              * Note that this is NOT great algorithmically. Redis uses
              * a single time event so it's not a problem but the right
              * way to do this is to add the new elements on head, and
              * to flag deleted elements in a special way for later
              * deletion (putting references to the nodes to delete into
              * another linked list). */                           //如果这个时间处理函数不再续执行，则从time event的链表中删除事件；否则，retval为继续执行的时间间隔（单位为ms），在当前的eevent struct的时间值上进行增加
             if (retval != AE_NOMORE) {
                 aeAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
             } else {
                 aeDeleteTimeEvent(eventLoop, id);
             }
             te = eventLoop->timeEventHead;
         } else {
             te = te->next;
         }
     }
     return processed;
 }
 
 /* Process every pending(悬而未决) time event, then every pending file event
  * (that may be registered by time event callbacks just processed).
  * Without special flags the function sleeps until some file event
  * fires, or when the next time event occurrs (if any).
  *
  * If flags is 0, the function does nothing and returns.
  * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
  * if flags has AE_FILE_EVENTS set, file events are processed.
  * if flags has AE_TIME_EVENTS set, time events are processed.
  * if flags has AE_DONT_WAIT set the function returns ASAP until all
  * the events that's possible to process without to wait are processed.
  *
  * The function returns the number of events processed. */
 int aeProcessEvents(aeEventLoop *eventLoop, int flags)
 {
     int processed = 0, numevents;
 
     /* Nothing to do? return ASAP */
     if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;
 
     /* Note that we want call select() even if there are no
      * file events to process as long as we want to process time
      * events, in order to sleep until the next time event is ready
      * to fire. */
     if (eventLoop->maxfd != -1 ||
         ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
         int j;
         aeTimeEvent *shortest = NULL;
         struct timeval tv, *tvp;
 
         if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
             shortest = aeSearchNearestTimer(eventLoop);
         if (shortest) {
             long now_sec, now_ms;
 
             /* Calculate the time missing for the nearest
              * timer to fire. */
             aeGetTime(&now_sec, &now_ms);
             tvp = &tv;
             tvp->tv_sec = shortest->when_sec - now_sec;
             if (shortest->when_ms < now_ms) {
                 tvp->tv_usec = ((shortest->when_ms+1000) - now_ms)*1000;
                 tvp->tv_sec --;
             } else {
                 tvp->tv_usec = (shortest->when_ms - now_ms)*1000;
             }
             if (tvp->tv_sec < 0) tvp->tv_sec = 0;
             if (tvp->tv_usec < 0) tvp->tv_usec = 0;  疑似有bug，比如当前时间为 1s +2ms  ，rtest时间为0s+1ms的case
         } else {
             /* If we have to check for events but need to return
              * ASAP because of AE_DONT_WAIT we need to se the timeout
              * to zero */
             if (flags & AE_DONT_WAIT) {
                 tv.tv_sec = tv.tv_usec = 0;
                 tvp = &tv;
             } else {
                 /* Otherwise we can block */
                 tvp = NULL; /* wait forever */
             }
         }
             //指定这个tvp是说这个tvp到期时，至少由time event可以执行
         numevents = aeApiPoll(eventLoop, tvp);
         for (j = 0; j < numevents; j++) {
             aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];
             int mask = eventLoop->fired[j].mask;
             int fd = eventLoop->fired[j].fd;
             int rfired = 0;
 
         /* note the fe->mask & mask & ... code: maybe an already processed
              * event removed an element that fired and we still didn't
              * processed, so we check if the event is still valid. */
             if (fe->mask & mask & AE_READABLE) {
                 rfired = 1;
                 fe->rfileProc(eventLoop,fd,fe->clientData,mask);
             }                //避免同一个注册函数被调用两次
             if (fe->mask & mask & AE_WRITABLE) {
                 if (!rfired || fe->wfileProc != fe->rfileProc)
                     fe->wfileProc(eventLoop,fd,fe->clientData,mask);
             }
             processed++;
         }
     }
     /* Check time events */
     if (flags & AE_TIME_EVENTS)
         processed += processTimeEvents(eventLoop);
 
     return processed; /* return the number of processed file/time events */
 }
     //对select的一个简单封装，没有太大意义
 /* Wait for millseconds until the given file descriptor becomes
  * writable/readable/exception */
 int aeWait(int fd, int mask, long long milliseconds) {
     struct timeval tv;
     fd_set rfds, wfds, efds;
     int retmask = 0, retval;
 
     tv.tv_sec = milliseconds/1000;
     tv.tv_usec = (milliseconds%1000)*1000;
     FD_ZERO(&rfds);
     FD_ZERO(&wfds);
     FD_ZERO(&efds);
 
     if (mask & AE_READABLE) FD_SET(fd,&rfds);
     if (mask & AE_WRITABLE) FD_SET(fd,&wfds);
     if ((retval = select(fd+1, &rfds, &wfds, &efds, &tv)) > 0) {
         if (FD_ISSET(fd,&rfds)) retmask |= AE_READABLE;
         if (FD_ISSET(fd,&wfds)) retmask |= AE_WRITABLE;
         return retmask;
     } else {
         return retval;
     }
 }
 
 void aeMain(aeEventLoop *eventLoop) {
     eventLoop->stop = 0;
     while (!eventLoop->stop) {
         if (eventLoop->beforesleep != NULL)
             eventLoop->beforesleep(eventLoop);
         aeProcessEvents(eventLoop, AE_ALL_EVENTS);
     }
 }
 
 char *aeGetApiName(void) {
     return aeApiName();
 }
 
 void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep) {
     eventLoop->beforesleep = beforesleep;
 }