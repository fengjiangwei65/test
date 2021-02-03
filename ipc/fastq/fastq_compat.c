/**********************************************************************************************************************\
*  文件： fastq_compat.c
*  介绍： 低时延队列 两套函数的接口 参见 fastq.h _FQ_NAME 定义
*  作者： 荣涛
*  日期：
*       2021年2月2日 创建文件
\**********************************************************************************************************************/



// FastQRing
struct _FQ_NAME(FastQRing) {
    unsigned long src;  //是 1- FASTQ_ID_MAX 的任意值
    unsigned long dst;  //是 1- FASTQ_ID_MAX 的任意值 二者不能重复

#ifdef FASTQ_STATISTICS //统计功能
    atomic64_t nr_enqueue;
    atomic64_t nr_dequeue;
#endif    
    
    unsigned int _size;
    size_t _msg_size;
    char _pad1[64]; //强制对齐，省的 cacheline 64 字节 的限制，下同
    volatile unsigned int _head;
    char _pad2[64];    
    volatile unsigned int _tail;
    char _pad3[64];    
    int _evt_fd; //队列eventfd通知
    char _ring_data[]; //保存实际对象
};


// 从 event fd 查找 ring 的最快方法
static  struct {
    struct _FQ_NAME(FastQRing) *_ring;
} _FQ_NAME(_evtfd_to_ring)[FD_SETSIZE] = {NULL};


//从 源module ID 和 目的module ID 到 _ring 最快的方法
struct _FQ_NAME(FastQModule) {
    bool already_register;
    int epfd; //epoll fd
    unsigned long module_id; //是 1- FASTQ_ID_MAX 的任意值
    unsigned int ring_size; //队列大小，ring 节点数
    unsigned int msg_size; //消息大小， ring 节点大小
    struct _FQ_NAME(FastQRing) **_ring;
};
static struct _FQ_NAME(FastQModule) *_FQ_NAME(_AllModulesRings) = NULL;

static  __attribute__((constructor(101))) _FQ_NAME(__FastQInitCtor) () {
    int i, j;
    
    LOG_DEBUG("Init _module_src_dst_to_ring.\n");
    _FQ_NAME(_AllModulesRings) = FastQMalloc(sizeof(struct _FQ_NAME(FastQModule))*(FASTQ_ID_MAX+1));
    for(i=0; i<=FASTQ_ID_MAX; i++) {
        _FQ_NAME(_AllModulesRings)[i].already_register = false;
        _FQ_NAME(_AllModulesRings)[i].module_id = i;
        _FQ_NAME(_AllModulesRings)[i].epfd = -1;
        _FQ_NAME(_AllModulesRings)[i].ring_size = 0;
        _FQ_NAME(_AllModulesRings)[i].msg_size = 0;
        
        _FQ_NAME(_AllModulesRings)[i]._ring = FastQMalloc(sizeof(struct _FQ_NAME(FastQRing)*)*(FASTQ_ID_MAX+1));
        for(j=0; j<=FASTQ_ID_MAX; j++) { 
            _FQ_NAME(_AllModulesRings)[i]._ring[j] = NULL;
        }
    }
}

always_inline  static struct _FQ_NAME(FastQRing) *
_FQ_NAME(__fastq_create_ring)(const int epfd, const unsigned long src, const unsigned long dst, 
                      const unsigned int ring_size, const unsigned int msg_size) {
    struct _FQ_NAME(FastQRing) *new_ring = FastQMalloc(sizeof(struct _FQ_NAME(FastQRing)) + ring_size*(msg_size+sizeof(size_t)));
    assert(new_ring);
    assert(epfd);
    
    LOG_DEBUG("Create ring %ld->%ld.\n", src, dst);
    new_ring->src = src;
    new_ring->dst = dst;
    new_ring->_size = ring_size - 1;
    
    new_ring->_msg_size = msg_size + sizeof(size_t);
    new_ring->_evt_fd = eventfd(0, EFD_CLOEXEC);
    assert(new_ring->_evt_fd); //都TMD没有fd了，你也是厉害
    
    LOG_DEBUG("Create module %ld event fd = %d.\n", dst, new_ring->_evt_fd);
    
    _FQ_NAME(_evtfd_to_ring)[new_ring->_evt_fd]._ring = new_ring;

    struct epoll_event event;
    event.data.fd = new_ring->_evt_fd;
    event.events = EPOLLIN; //必须采用水平触发
    epoll_ctl(epfd, EPOLL_CTL_ADD, event.data.fd, &event);
    LOG_DEBUG("Add eventfd %d to epollfd %d.\n", new_ring->_evt_fd, epfd);

#ifdef FASTQ_STATISTICS //统计功能

    atomic64_init(&new_ring->nr_dequeue);
    atomic64_init(&new_ring->nr_enqueue);
#endif
    return new_ring;
    
}

                      
/**
 *  FastQCreateModule - 注册消息队列
 *  
 *  param[in]   moduleID    模块ID， 范围 1 - FASTQ_ID_MAX
 *  param[in]   msgMax      该模块 的 消息队列 的大小
 *  param[in]   msgSize     最大传递的消息大小
 */
always_inline void 
_FQ_NAME(FastQCreateModule)(const unsigned long module_id, const unsigned int ring_size, const unsigned int msg_size) {
    assert(module_id <= FASTQ_ID_MAX);
    
    int i, j;
    
    if(_FQ_NAME(_AllModulesRings)[module_id].already_register) {
        LOG_DEBUG("Module %ld already register.\n", module_id);
        return false;
    }
    _FQ_NAME(_AllModulesRings)[module_id].already_register = true;
    _FQ_NAME(_AllModulesRings)[module_id].epfd = epoll_create(1);
    assert(_FQ_NAME(_AllModulesRings)[module_id].epfd);
        
    LOG_DEBUG("Create module %ld epoll fd = %d.\n", module_id, _FQ_NAME(_AllModulesRings)[module_id].epfd);
    
    _FQ_NAME(_AllModulesRings)[module_id].ring_size = __power_of_2(ring_size);
    _FQ_NAME(_AllModulesRings)[module_id].msg_size = msg_size;

    /* 当源模块未初始化时又想向目的模块发送消息 */
    _FQ_NAME(_AllModulesRings)[module_id]._ring[0] = _FQ_NAME(__fastq_create_ring)(_FQ_NAME(_AllModulesRings)[module_id].epfd, 0, module_id,\
                                                                __power_of_2(ring_size), msg_size);

    for(i=1; i<=FASTQ_ID_MAX && i != module_id; i++) {
        if(!_FQ_NAME(_AllModulesRings)[i].already_register) {
            continue;
        }
        _FQ_NAME(_AllModulesRings)[module_id]._ring[i] = _FQ_NAME(__fastq_create_ring)(_FQ_NAME(_AllModulesRings)[module_id].epfd, i, module_id,\
                                                                    __power_of_2(ring_size), msg_size);
        if(!_FQ_NAME(_AllModulesRings)[i]._ring[module_id]) {
            _FQ_NAME(_AllModulesRings)[i]._ring[module_id] = _FQ_NAME(__fastq_create_ring)(_FQ_NAME(_AllModulesRings)[i].epfd, module_id, i, \
                                                    _FQ_NAME(_AllModulesRings)[module_id].ring_size, _FQ_NAME(_AllModulesRings)[i].msg_size);
        }
    }
}

/**
 *  __FastQSend - 公共发送函数
 */
always_inline static bool 
_FQ_NAME(__FastQSend)(struct _FQ_NAME(FastQRing) *ring, const void *msg, const size_t size) {
    assert(ring);
    assert(size <= (ring->_msg_size - sizeof(size_t)));

    unsigned int h = (ring->_head - 1) & ring->_size;
    unsigned int t = ring->_tail;
    if (t == h) {
        return false;
    }

    LOG_DEBUG("Send %ld->%ld.\n", ring->src, ring->dst);

    char *d = &ring->_ring_data[t*ring->_msg_size];
    
    memcpy(d, &size, sizeof(size));
    LOG_DEBUG("Send >>> memcpy size = %d\n", size);
    memcpy(d + sizeof(size), msg, size);
    LOG_DEBUG("Send >>> memcpy addr = %x\n", *(unsigned long*)(d + sizeof(size)));

    // Barrier is needed to make sure that item is updated 
    // before it's made available to the reader
    mwbarrier();
#ifdef FASTQ_STATISTICS //统计功能
    atomic64_inc(&ring->nr_enqueue);
#endif

    ring->_tail = (t + 1) & ring->_size;
    return true;
}

/**
 *  FastQSend - 发送消息（轮询直至成功发送）
 *  
 *  param[in]   from    源模块ID， 范围 1 - FASTQ_ID_MAX 
 *  param[in]   to      目的模块ID， 范围 1 - FASTQ_ID_MAX
 *  param[in]   msg     传递的消息体
 *  param[in]   size    传递的消息大小
 *
 *  return 成功true （轮询直至发送成功，只可能返回 true ）
 *
 *  注意：from 和 to 需要使用 FastQCreateModule 注册后使用
 */
always_inline bool 
_FQ_NAME(FastQSend)(unsigned int from, unsigned int to, const void *msg, size_t size) {
    struct _FQ_NAME(FastQRing) *ring = _FQ_NAME(_AllModulesRings)[to]._ring[from];
    while (!_FQ_NAME(__FastQSend)(ring, msg, size)) {__relax();}
    
    eventfd_write(ring->_evt_fd, 1);
    
    LOG_DEBUG("Send done %ld->%ld, event fd = %d, msg = %p.\n", ring->src, ring->dst, ring->_evt_fd, msg);
    return true;
}

/**
 *  FastQTrySend - 发送消息（尝试向队列中插入，当队列满是直接返回false）
 *  
 *  param[in]   from    源模块ID， 范围 1 - FASTQ_ID_MAX 
 *  param[in]   to      目的模块ID， 范围 1 - FASTQ_ID_MAX
 *  param[in]   msg     传递的消息体
 *  param[in]   size    传递的消息大小
 *
 *  return 成功true 失败false
 *
 *  注意：from 和 to 需要使用 FastQCreateModule 注册后使用
 */
always_inline bool 
_FQ_NAME(FastQTrySend)(unsigned int from, unsigned int to, const void *msg, size_t size) {
    struct _FQ_NAME(FastQRing) *ring = _FQ_NAME(_AllModulesRings)[to]._ring[from];
    bool ret = _FQ_NAME(__FastQSend)(ring, msg, size);
    if(ret) {
        eventfd_write(ring->_evt_fd, 1);
        LOG_DEBUG("Send done %ld->%ld, event fd = %d.\n", ring->src, ring->dst, ring->_evt_fd);
    }
    return ret;
}

always_inline static bool 
_FQ_NAME(__FastQRecv)(struct _FQ_NAME(FastQRing) *ring, void *msg, size_t *size) {

    unsigned int t = ring->_tail;
    unsigned int h = ring->_head;
    if (h == t) {
//        LOG_DEBUG("Recv <<< %ld->%ld.\n", ring->src, ring->dst);
        return false;
    }
    
    LOG_DEBUG("Recv <<< %ld->%ld.\n", ring->src, ring->dst);

    char *d = &ring->_ring_data[h*ring->_msg_size];

    size_t recv_size;
    memcpy(&recv_size, d, sizeof(size_t));
    LOG_DEBUG("Recv <<< memcpy recv_size = %d\n", recv_size);
    assert(recv_size <= *size && "buffer too small");
    *size = recv_size;
    LOG_DEBUG("Recv <<< size\n");
    memcpy(msg, d + sizeof(size_t), recv_size);
    LOG_DEBUG("Recv <<< memcpy addr = %x\n", *(unsigned long*)(d + sizeof(size_t)));

    // Barrier is needed to make sure that we finished reading the item
    // before moving the head
    mbarrier();
    LOG_DEBUG("Recv <<< mbarrier\n");
#ifdef FASTQ_STATISTICS //统计功能
    atomic64_inc(&ring->nr_dequeue);
#endif

    ring->_head = (h + 1) & ring->_size;
    return true;
}

/**
 *  FastQRecv - 接收消息
 *  
 *  param[in]   from    从模块ID from 中读取消息， 范围 1 - FASTQ_ID_MAX 
 *  param[in]   handler 消息处理函数，参照 msg_handler_t 说明
 *
 *  return 成功true 失败false
 *
 *  注意：from 需要使用 FastQCreateModule 注册后使用
 */
always_inline  bool 
_FQ_NAME(FastQRecv)(unsigned int from, msg_handler_t handler) {

    eventfd_t cnt;
    int nfds;
    struct epoll_event events[8];
    
    unsigned long addr;
    size_t size = sizeof(unsigned long);
    struct _FQ_NAME(FastQRing) *ring = NULL;

    while(1) {
        LOG_DEBUG("Recv start >>>>  epoll fd = %d.\n", _FQ_NAME(_AllModulesRings)[from].epfd);
        nfds = epoll_wait(_FQ_NAME(_AllModulesRings)[from].epfd, events, 8, -1);
        LOG_DEBUG("Recv epoll return epfd = %d.\n", _FQ_NAME(_AllModulesRings)[from].epfd);
        
        for(;nfds--;) {
            ring = _FQ_NAME(_evtfd_to_ring)[events[nfds].data.fd]._ring;
            eventfd_read(events[nfds].data.fd, &cnt);
//            if(cnt>1) {
//                printf("cnt = =%ld\n", cnt);
//            }
            LOG_DEBUG("Event fd %d read return cnt = %ld.\n", events[nfds].data.fd, cnt);
            for(; cnt--;) {
                LOG_DEBUG("<<< _FQ_NAME(__FastQRecv).\n");
                while (!_FQ_NAME(__FastQRecv)(ring, &addr, &size)) {
                    __relax();
                }
                LOG_DEBUG("<<< _FQ_NAME(__FastQRecv) addr = %x, size = %ld.\n", addr, size);
                handler(addr, size);
                LOG_DEBUG("<<< _FQ_NAME(__FastQRecv).\n");
                addr = 0;
                size = sizeof(unsigned long);
            }

        }
    }
    return true;
}

/**
 *  FastQDump - 显示信息
 *  
 *  param[in]   fp    文件指针
 */
always_inline void 
_FQ_NAME(FastQDump)(FILE*fp) {
    if(unlikely(!fp) || unlikely(!fp)) {
        fp = stderr;
    }
    
    int i, j;
    
    for(i=1; i<=FASTQ_ID_MAX; i++) {
        if(!_FQ_NAME(_AllModulesRings)[i].already_register) {
            continue;
        }
#ifdef FASTQ_STATISTICS //统计功能
        atomic64_t module_total_msgs[2];
        atomic64_init(&module_total_msgs[0]); //总入队数量
        atomic64_init(&module_total_msgs[1]); //总出队数量
#endif        
        
        fprintf(fp, "\n------------------------------------------\n"\
                    "ID: %3i, msgMax %4u, msgSize %4u\n"\
                    "\t from-> to   %10s %10s"
#ifdef FASTQ_STATISTICS //统计功能
                    " %16s %16s %16s "
#endif                    
                    "\n"
                    , i, 
                    _FQ_NAME(_AllModulesRings)[i].ring_size, 
                    _FQ_NAME(_AllModulesRings)[i].msg_size, 
                    "head", 
                    "tail"
#ifdef FASTQ_STATISTICS //统计功能
                    , "enqueue", "dequeue", "current"
#endif                    
                    );
        
        for(j=0; j<=FASTQ_ID_MAX; j++) { 
            if(_FQ_NAME(_AllModulesRings)[i]._ring[j]) {
                fprintf(fp, "\t %4i->%-4i  %10u %10u"
#ifdef FASTQ_STATISTICS //统计功能
                            " %16u %16u %16u"
#endif                            
                            "\n" \
                            , j, i, 
                            _FQ_NAME(_AllModulesRings)[i]._ring[j]->_head, 
                            _FQ_NAME(_AllModulesRings)[i]._ring[j]->_tail
#ifdef FASTQ_STATISTICS //统计功能
                            ,atomic64_read(&_FQ_NAME(_AllModulesRings)[i]._ring[j]->nr_enqueue),
                            atomic64_read(&_FQ_NAME(_AllModulesRings)[i]._ring[j]->nr_dequeue),
                            atomic64_read(&_FQ_NAME(_AllModulesRings)[i]._ring[j]->nr_enqueue)
                                -atomic64_read(&_FQ_NAME(_AllModulesRings)[i]._ring[j]->nr_dequeue)
#endif                                
                            );
#ifdef FASTQ_STATISTICS //统计功能
                atomic64_add(&module_total_msgs[0], atomic64_read(&_FQ_NAME(_AllModulesRings)[i]._ring[j]->nr_enqueue));
                atomic64_add(&module_total_msgs[1], atomic64_read(&_FQ_NAME(_AllModulesRings)[i]._ring[j]->nr_dequeue));
#endif                
            }
        }
        
#ifdef FASTQ_STATISTICS //统计功能
        fprintf(fp, "\t Total enqueue %16u, dequeue %16u\n", atomic64_read(&module_total_msgs[0]), atomic64_read(&module_total_msgs[1]));
#endif
    }
}

