#include "Webserv.hpp"


#include <iostream>

// 构造函数前面绝对没有任何 void 或者是返回值类型！
ServerManager::ServerManager(const std::vector<ServerConfig> &configs)
    : _server_configs(configs) // 优雅落盘：初始化列表完美注入物理配置资产
{
    // 💡 这里可以用初始化的资产做点温馨的冷启动日志
    std::cout << "[ServerManager] WebServ engine pre-loaded with "
              << _server_configs.size() << " virtual servers." << std::endl;
}

// 斩断所有堆上开辟的服务器物理套接字指针
ServerManager::~ServerManager()
{
    for (size_t i = 0; i < this->_listen_sockets.size(); ++i)
    {
        if (this->_listen_sockets[i] != NULL)
        {
            delete this->_listen_sockets[i];
        }
    }
    this->_listen_sockets.clear();
    std::cout << "[ServerManager] Engine completely shutdown and memory released." << std::endl;
}
/*
函数用途：物理拉起 Webserv 网络的奠基点火仪式（巢穴孵化），全量启动底层服务器级网络套接字的物理实例化。
参数与变量：
- setupSockets (类内部核心函数)：专职读取配置文件、循环创建 Socket、绑定端口（bind）并将其切入被动监听状态（listen）的基建车间。
实现逻辑：
1. 宣告主权：向控制台抛出高亮初始化日志，宣告大管家网络骨架正式破土动工。
2. 物理孵化：果断向下调用 setupSockets 核心基建车间，将配置文件里规划的所有虚拟端口，全量具象化为操作系统的 ListenFD（监听套接字），
   并手工将这些元老级 FD 编入全局轮询名册（_poll_fds），为后续 run() 引擎进入多路复用矩阵焊死第一块钢铁基石！
*/
void ServerManager::init()
{
    std::cout << "[ServerManager] Initializing network sockets..." << std::endl;
    this->setupSockets();
}

/*
函数用途：全量解析服务器配置名册，物理孵化各端口的非阻塞监听套接字（ListenFD），并作为元老级哨兵首批编入多路复用大循环。
参数与变量：
- _server_configs (类内部常驻容器)：std::vector<ServerConfig>，由配置文件加载而来的全量虚拟主机配置蓝图。
- handled_ports (局部暂存容器)：std::vector<int>，运行时动态端口物理去重探测账本，严防重复 bind 导致系统血崩。
- srv_sock (局部指针变量)：ServerSocket*，Socket 封装工厂实体，负责底层的 socket()、setsockopt()、bind() 和 listen() 铁血四部曲。
- _listen_sockets (类内部常驻容器)：std::vector<ServerSocket*>，大管家物理持有的基础套接字资产舱。
- _listen_socket_map (类内部常驻容器)：std::map<int, ServerConfig>，专职记录“监听 FD -> 专属配置”的因果反查账本。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的多路复用全局核心轮询监视名册。
实现逻辑：
1. 物理去重防线：纵向遍历配置名册，提取端口与 host。拿着当前端口突袭 handled_ports 账本，若发现多主机共享同一端口，则优雅跳过重复绑定，实现多虚拟主机并网。
2. 工厂实例化：未命中的新端口交由 ServerSocket 工厂物理孵化，一枪执行内部 setup()。将其切入非阻塞被动监听状态，宣告本地网络主权。
3. 账本入籍与反查绑定：将生成的 ListenFD 塞进资产舱，同时在 _listen_socket_map 账本里留下因果烙印，确保新进线客户端能精准识别自己归属于哪台虚拟主机。
4. 挂载元老级哨兵：实例化 struct pollfd，注入 POLLIN 读雷达方向，物理清空回执层，将其作为地基骨架 push_back 灌入核心名册，正式拉开帝国防御网序幕！
*/
void ServerManager::setupSockets()
{
    std::vector<int> handled_ports;

    for (size_t i = 0; i < _server_configs.size(); ++i)
    {
        int port = _server_configs[i].port;
        std::string host = _server_configs[i].host;

        // 物理去重探测
        bool port_duplicate = false;
        for (size_t p = 0; p < handled_ports.size(); ++p)
        {
            if (handled_ports[p] == port)
            {
                port_duplicate = true;
                break;
            }
        }
        if (port_duplicate)
        {
            std::cout << "[ServerManager] Multi-server configuration detected for port " << port << " (Skipping duplicate bind)" << std::endl;
            continue;
        }

        // 工厂实例化
        ServerSocket *srv_sock = new ServerSocket(host, port);
        srv_sock->setup();
        int listenFd = srv_sock->getFd();
        std::cout << "[ServerManager] Successfully listening on " << host << ":" << port << " (FD: " << listenFd << ")" << std::endl;

        this->_listen_sockets.push_back(srv_sock);
        handled_ports.push_back(port);
        _listen_socket_map[listenFd] = _server_configs[i];

        // 挂载 poll 哨兵
        struct pollfd pfd;
        pfd.fd = listenFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        _poll_fds.push_back(pfd);
    }
}

/*
函数用途：在拉起多路复用大闸前执行前置洗舱，全量剔除名册中已被打上 -1 熔断标记的废弃 FD，完成名册物理瘦身。
参数与变量：
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的多路复用全局核心轮询监视名册。
实现逻辑：
1. 动态变长正序扫描：纵向遍历全局轮询名册 _poll_fds。为了完美应对 vector 执行 erase 擦除导致的元素整体物理前移，
   必须在指针平移上采用“不进则删”的变长控制策略。
2. 物理销户与名册瘦身：若发现当前节点的 fd 已经沦为 -1（说明它已在之前的 handle 业务或熔断车间中宣告破产并关闭），
   则果断将其从 vector 名册中物理擦除，且索引 i 保持原地不动（用来精准迎击前移上来的下一个新节点）；否则，i 自增推进。
3. 筑坝防线：此举确保下一轮 executePoll 调用 ::poll 时，喂给 Linux 内核的名册全部由百分之百健康的活跃 FD 组成，
   彻底封杀因为带入脏 FD 导致内核疯狂报错、CPU 空转忙轮询的性能隐患。
*/
void ServerManager::prePollCleanup()
{
    for (size_t i = 0; i < this->_poll_fds.size();)
    {
        if (this->_poll_fds[i].fd == -1)
        {
            this->_poll_fds.erase(this->_poll_fds.begin() + i);
        }
        else
        {
            ++i;
        }
    }
}

/*
函数用途：物理拉起多路复用核心轮询大闸（::poll），并用分级弹性防线死守信号中断带来的系统震荡。
参数与变量：
- retries (传入引用参数)：外部主循环托付的防御性崩溃计数器，用来记录连续发生系统中断的次数。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的全局多路复用核心轮询监视名册。
- ret (局部变量)：Linux 内核本次弹回的就绪事件总数，或代表负面危机的系统级错误代码。
实现逻辑：
1. 传入 -1 物理阻塞参数，强行命令主线程让出 CPU，陷入非阻塞全天候休眠，直到雷达名册中任意 FD 触发就绪。
2. 熔断危机拦截：若 poll 返回负数报错，启动分级弹性防线。在 3 次以内视作常规信号（如 EINTR）引发的轻微震荡，
   计数器自增并安全返回 0，迫使主循环无感刷新；一旦跨过 3 次红线则触发彻底熔断，断开危机大闸返回 -1。
3. 黄金复位并网：若成功捕捉到哪怕一字节的有效网络事件，立刻将崩溃计数器强行物理复位归零，重新开启帝国防御！
*/
int ServerManager::executePoll(int &retries)
{
    int ret = poll(&this->_poll_fds[0], this->_poll_fds.size(), -1);

    if (ret < 0)
    {
        if (retries < 3)
        {
            retries++;
            return 0;
        }
        return -1;
    }

    retries = 0; // 成功捕获事件，物理复位防崩溃计数器
    return ret;
}

/*
函数用途：在多路复用雷达网上动态激活目标客户端的写事件（POLLOUT），宣告其发件箱货物已齐。
参数与变量：
- clientFd (传入参数)：当前已经装订完毕、眼巴巴等着把 HTTP 报文派发出去的客户端网络套接字。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的多路复用核心轮询监视名册。
实现逻辑：
1. 线性扫描全局轮询名册 _poll_fds，精准定位该 clientFd 在内核名册中入籍的物理坑位。
2. 锁定位置后，在原有的读雷达（POLLIN）基础上，强行用位或操作追加追加绑定 POLLOUT 监听标记。
3. 此举会在下一轮大循环中激活内核通知，将控制权平滑扭送至核心发货车间（handleClientWrite），实现高效控制。
*/
void ServerManager::enableClientWriteEvent(int clientFd)
{
    size_t i = 0;
    while (i < this->_poll_fds.size())
    {
        if (this->_poll_fds[i].fd == clientFd)
        {
            this->_poll_fds[i].events |= POLLOUT;
            break;
        }
        ++i;
    }
}

/*
函数用途：判定当前就绪的 fd 是否属于 CGI 子进程与主进程通信的异步物理管道读/写端。
参数与变量：
- fd (传入参数)：多路复用大循环当前弹回、亟待辨别身份的就绪文件描述符。
- _cgi_read_fd_to_client_map (类内部常驻容器)：std::map<int, int>，专职捕捞读端管道的雷达映射账本。
- _cgi_write_fd_to_client_map (类内部常驻容器)：std::map<int, int>，专职输送 POST 请求体的写端渠道映射账本。
实现逻辑：
1. 分别在读端账本（_cgi_read_fd_to_client_map）与写端账本（_cgi_write_fd_to_client_map）中发起红黑树雷达反查。
2. 只要在任意一个反查专属账本中挂号留痕（即未命中 end()），说明其身上背负着 CGI 契约，当场判为系统特权管道，返回 true。
3. 若两处账本均查无此人，说明它是普通的客户端、监听套接字或常规静态资源，返回 false。
*/
bool ServerManager::isCgiPipeFd(int fd)
{
    return (this->_cgi_read_fd_to_client_map.find(fd) != this->_cgi_read_fd_to_client_map.end()) ||
           (this->_cgi_write_fd_to_client_map.find(fd) != this->_cgi_write_fd_to_client_map.end());
}

/*
函数用途：将新生的物理文件描述符（Socket 或 Pipe）正式编入多路复用大循环，开启全天候事件监控。
参数与变量：
- fd (传入参数)：刚破壳而出（通过 accept 或 pipe 物理孵化）且亟待编入帝国防御网的底层文件描述符。
- events (传入参数)：预设的监控雷达事件方向，如代表接收数据的 POLLIN，或代表高效发货的 POLLOUT。
- pfd (局部变量)：系统原生 struct pollfd 结构体，作为大管家与 Linux 内核进行主权沟通的物理载体。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的全局多路复用核心轮询监视名册。
实现逻辑：
1. 实例化一个物理 pollfd 节点，绑定目标文件描述符，锁定数据对流通道。
2. 灌入指定的监听事件类型（events），并手工强行将内核回执层（revents）洗白清零，彻底封杀幽灵事件的误触触发。
3. 执行 push_back 操作让该资产在 _poll_fds 名册里物理入籍，在下一轮 poll 轮询中正式享受多路复用全量护航！
*/
void ServerManager::registerFdToPoll(int fd, short events)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events; // 传入 POLLIN
    pfd.revents = 0;     // 清空内核回执，防止幽灵触发

    this->_poll_fds.push_back(pfd); // 正式入籍大循环名册

    std::cout << "[ServerManager] FD " << fd << " successfully registered to poll tree." << std::endl;
}

/*
函数用途：作为 CGI 战后善后总务车间，专职以非阻塞姿态回收子进程遗骸、物理注销残留写端管道，并复位连接的 CGI 状态位。
参数与变量：
- conn (指针参数)：Connection*，当前正在处理 CGI 交互的客户端物理连接实体。
- status (局部变量)：int，用于接收 waitpid 提取出的子进程退出状态码。
- ret (局部变量)：pid_t，waitpid 系统调用的物理回执，用于判定子进程是否已经死亡。
实现逻辑：
1. 🧹 管道残渣清场：校验并物理关闭未耗尽或遗留的 POST 写端管道（cgi_write_fd），将其指针恢复为 -1。
2. 🛡️ 弹性非阻塞收尸（WNOHANG 护盾）：
   - 调用 waitpid(conn->cgi_pid, &status, WNOHANG)。如果子进程已经顺畅死亡，瞬间回收其 PCB 尸体；
   - 万一子进程因为某种原因（如死循环、线程挂起）尚未死透（ret == 0），为了绝不卡死主线程的 poll 大闸，当场果断补一枪 ::kill(conn->cgi_pid, SIGKILL) 物理毁灭打击，并再次 waitpid 强制秒级收尸！
3. 🔄 状态印记复位：将 cgi_pid 归零重置为 -1，并将 is_cgi 标志切回 false，为该 Connection 承接下一轮 HTTP 交互清理好物理赛道。
*/
void ServerManager::_cleanupCgiResources(Connection *conn)
{
    if (conn == NULL)
        return;

    // 1. 🧹 物理关闭 POST 写端管道残渣（若未关闭）
    if (conn->cgi_write_fd != -1)
    {
        ::close(conn->cgi_write_fd);
        conn->cgi_write_fd = -1;
    }

    // 2. 🪓 铁血回收子进程遗骸（带 WNOHANG 非阻塞护盾）
    if (conn->cgi_pid > 0)
    {
        int status = 0;
        // 💡 WNOHANG 护盾：尝试非阻塞收尸，绝不阻塞主线程
        pid_t ret = ::waitpid(conn->cgi_pid, &status, WNOHANG);

        if (ret == 0)
        {
            // 🚨 极端防御：子进程关了管道但还没死透！
            // 发送物理毁灭打击 SIGKILL，补一枪秒级收尸
            ::kill(conn->cgi_pid, SIGKILL);
            ::waitpid(conn->cgi_pid, NULL, 0); // 已经被 SIGKILL 必死无疑，瞬间收尸
        }
        
        // 标记子进程 PID 已经物理注销
        conn->cgi_pid = -1;
    }

    // 3. 🔄 彻底复位 CGI 状态标定
    conn->is_cgi = false;
}

/*
函数用途：物理接管并清洗来自 CGI 子进程管道写端喷射出的全量原始脏报文，直至触发 EOF 或熔断，随后实现资产加冕与主权并网。
参数与变量：
- cgiReadFd (传入参数)：多路复用大循环当前弹回、代表子进程数据出货完毕或触发挂断信号的读端物理管道描述符。
- poll_idx (传入参数)：该读端管道在全局轮询监视名册 _poll_fds 中入籍的当前物理坑位索引。
- conn (局部指针变量)：Connection*，顺藤摸瓜反查抓取到的、正在苦苦等待本次 CGI 动态网页资产的客户端核心连接载体。
- cgi_res (局部对象变量)：Response，面向对象属性分拣车间，专职对管道原始残渣实施协议级剥离、洗白和长度丈量。
- _cgi_read_fd_to_client_map (类内部常驻容器)：std::map<int, int>，专职捕捞读端管道与客户端对应关系的因果雷达账本。
实现逻辑：
1. 因果反查：拿着 cgiReadFd 去读端专属账本里检索。若查无此人，则当场就地销毁管道并剔除名册；若命中，则顺藤摸瓜提取出对应的 clientFd 与连接实体。
2. 龙卷风抽水循环：强攻非阻塞管道，通过 while (true) 循环用 read 疯狂向内核缓冲区舀水。
   - bytesRead > 0：将捕捞出来的原始字节安全追加注入 conn->write_buffer 暂存箱。
   - bytesRead == -1：若遭遇 EAGAIN 或 EWOULDBLOCK 则安全折返，等下一次数据就绪；若遭信号打断则坚决继续强抽。
   - bytesRead == 0 (黄金落幕终点)：代表子进程交货完毕或写端物理关闭，正式切入清仓结算阶段。
3. 黄金清洗并网：将攒满脏数据的缓冲区打包送进 Response 洗白车间（parseCgiOutput），分拣普通 Headers 与 Body，反查 Status 状态并重新加冕。
   随后用 swap 彻底物理排空并翻新客户端发件箱，将重构出的国际航空级标准 HTTP 满血响应一次性灌回发件箱！
4. 资产结算与资源回收：将该读端管道、POST 写端管道从多路复用名册中物理销户并斩断；通过 waitpid 铁血回收子进程遗骸，严防僵尸进程。
5. 临门一脚：调用 enableClientWriteEvent 追加客户端写雷达监听（POLLOUT），让标准发货车间在下一轮大循环中将满血货包一枪弹射给浏览器！
*/
void ServerManager::handleCgiPipeRead(int cgiReadFd, size_t poll_idx)
{
    // 1. 【顺藤摸瓜】：反查因果契约
    std::map<int, int>::iterator it = this->_cgi_read_fd_to_client_map.find(cgiReadFd);
    if (it == this->_cgi_read_fd_to_client_map.end())
    {
        ::close(cgiReadFd);
        this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
        return;
    }
    int clientFd = it->second;
    Connection *conn = _connections[clientFd];

    char buffer[4096];

    // 🚀 2. 【核心升级：龙卷风抽水循环】
    while (true)
    {
        ssize_t bytesRead = ::read(cgiReadFd, buffer, sizeof(buffer));

        if (bytesRead > 0)
        {
            // 🛡️ 物理隔离：只往 CGI 专用物料箱追加！发件箱 write_buffer 保持绝对净空！
            conn->cgi_output_buffer.append(buffer, static_cast<size_t>(bytesRead));
            std::cout << "[CGI Reader] Sucked " << bytesRead << " bytes from pipe fd " << cgiReadFd << " to client " << clientFd << std::endl;
            continue;
        }
        else if (bytesRead == 0)
        {
            // 🏁 3. 子进程吐货完毕（EOF 闭环）
            std::cout << "[CGI Reader] Reached EOF for pipe fd " << cgiReadFd << "." << std::endl;

            // ============================================================
            //                  黄金清洗并网安全车间 
            // ============================================================
            
            // 💡 纯净组装 Response 实体
            Response cgi_res = buildCgiResponse(conn->request, conn->cgi_output_buffer);

            // 强力清空已用完的物料箱与发件箱旧内存
            std::string().swap(conn->cgi_output_buffer);
            std::string().swap(conn->write_buffer);

            // 💎 临门一脚：将 Response 洗白组装出来的满血高级 HTTP 报文，一次性安全注入发件箱！
            conn->write_buffer = cgi_res.responseToString();

            // 4. 注销并关闭读端管道
            ::close(cgiReadFd);
            this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
            this->_cgi_read_fd_to_client_map.erase(it);
            conn->cgi_read_fd = -1;

            // 💡 物理双保险：连带清场可能残留的 POST 写端管道，严防 poll 雷达网悬空 FD
            if (conn->cgi_write_fd != -1)
            {
                this->_eraseFdFromPoll(conn->cgi_write_fd);
                this->_cgi_write_fd_to_client_map.erase(conn->cgi_write_fd);
                ::close(conn->cgi_write_fd);
                conn->cgi_write_fd = -1;
            }

            // 5. 资源清理与子进程回收（带 WNOHANG 护盾）
            this->_cleanupCgiResources(conn);

            // 🚀 货齐了，拉起客户端写事件，下一轮 TICK 完美发货！
            this->enableClientWriteEvent(clientFd);
            return; 
        }
        else
        {
            // bytesRead == -1：遇到了底层的分水岭
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; // 内核缓冲区抽干，正常退出，等下一次数据来
            if (errno == EINTR)
                continue; // 被信号打断，继续抽

            // ❌ 发生真正的系统级读取错误，切入 500 熔断
            std::cerr << "[CGI Reader] System read error on fd " << cgiReadFd << ", breaking conduit." << std::endl;

            // 物理清空脏物料箱与发件箱
            std::string().swap(conn->cgi_output_buffer);
            std::string().swap(conn->write_buffer);

            conn->response.createResponse(500, "CGI Read Error", conn->config.error_pages);
            conn->write_buffer = conn->response.responseToString();

            // 物理擦除读端管道
            ::close(cgiReadFd);
            this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
            this->_cgi_read_fd_to_client_map.erase(it);
            conn->cgi_read_fd = -1;

            // 物理连带擦除写端管道
            if (conn->cgi_write_fd != -1)
            {
                this->_eraseFdFromPoll(conn->cgi_write_fd);
                this->_cgi_write_fd_to_client_map.erase(conn->cgi_write_fd);
                ::close(conn->cgi_write_fd);
                conn->cgi_write_fd = -1;
            }

            // 🪓 统一交由善后车间：斩杀子进程并回收（带 WNOHANG 护盾，无阻塞隐患）
            this->_cleanupCgiResources(conn);

            this->enableClientWriteEvent(clientFd);
            return;
        }
    }
}

/*
函数用途：物理接管多路复用写雷达（POLLOUT）弹回的 CGI 写端管道，非阻塞、卡尺切片地向子进程标准输入（stdin）灌入 POST 请求体，直至喂饱触发 EOF 闭环。
参数与变量：
- cgiWriteFd (传入参数)：多路复用大循环当前弹回、代表子进程输入管道缓冲区空闲、急需喂入数据的写端物理描述符。
- poll_idx (传入参数)：该写端管道在全局轮询监视名册 _poll_fds 中入籍的当前物理坑位索引。
- conn (局部指针变量)：Connection*，顺藤摸瓜反查抓取到的、目前正持有 POST 原始 Body 资产的客户端核心连接实体。
- body (局部常引用变量)：std::string，客户端发来的、亟待灌注进 Python 等动态脚本的请求体原始数据。
- _cgi_write_fd_to_client_map (类内部常驻容器)：std::map<int, int>，经过正名典礼、专职输送 POST 请求体的写端渠道映射账本。
实现逻辑：
1. 因果反查：拿着 cgiWriteFd 突袭写端专属账本。若查无此人，则作为孤儿管道防卫性就地关闭并擦除名册；若命中，则提取出对应的 clientFd 与连接实体。
2. 非阻塞卡尺切片：精确校准 conn->cgi_body_bytes_sent 计数器，将剩余运力（remaining）以每次最多 4096 字节的“卡尺块”进行物理分割，调用 ::write 强攻写入。
3. 边缘震荡防御：
   - bytes_written > 0：推进已发送计数器，保留 POLLOUT 标志让下一轮循环继续推进。
   - bytes_written == -1：若遭遇 EAGAIN/EWOULDBLOCK（内核缓冲区满）或 EINTR（系统信号打断），则不算错，直接撤退，等下一轮大循环再度进来喂食。
   - 若发生其他底层物理破裂（如 EPIPE，子进程死掉），则打断对流，切入 ERROR_FUSE 报错熔断车间，下发 500 报文。
4. 完工收网（临门一脚）：当所有 Body 喂饱子进程后，主动执行 ::close(cgiWriteFd) 砍断写端管道。此举是给子进程的标准输入物理派发一枚完美的 EOF（文件终点标记），
   彻底唤醒挂起等待的 Python 脚本（如 sys.stdin.read()），迫使其开启动态计算！随后在名册与写端账本中彻底将其擦除销户。
*/
void ServerManager::handleCgiPipeWrite(int cgiWriteFd, size_t poll_idx)
{
    // 1. 【顺藤摸瓜】：一枪反查因果契约，明确知道这个写端管道是在伺候哪个客户端
    std::map<int, int>::iterator it = this->_cgi_write_fd_to_client_map.find(cgiWriteFd);
    if (it == this->_cgi_write_fd_to_client_map.end())
    {
        // 孤儿写端管道，防卫性物理关闭，从雷达网里擦除
        ::close(cgiWriteFd);
        this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
        return;
    }
    int clientFd = it->second;
    Connection *conn = this->_connections[clientFd];
    const std::string &body = conn->request.getBody();

    // 2. 极端边界防御：如果进线时发现原本就已经喂饱了，直接走清场流程
    if (conn->cgi_body_bytes_sent >= body.size())
    {
        goto CLOSE_WRITE_Conduit;
    }

    // 3. 【非阻塞卡尺切片】：计算剩余运力，每次最多安全喂入 4096 字节
    {
        const char *data_ptr = body.data() + conn->cgi_body_bytes_sent;
        size_t remaining = body.size() - conn->cgi_body_bytes_sent;
        size_t chunk_size = (remaining > 4096) ? 4096 : remaining;

        // 强攻非阻塞物理管道写入
        ssize_t bytes_written = ::write(cgiWriteFd, data_ptr, chunk_size);

        if (bytes_written > 0)
        {
            conn->cgi_body_bytes_sent += bytes_written;
            std::cout << "[CGI Writer] Fed " << bytes_written << " bytes of body to CGI fd "
                      << cgiWriteFd << ". Total: " << conn->cgi_body_bytes_sent << "/" << body.size() << std::endl;
        }
        else if (bytes_written == -1)
        {
            // 如果遇到系统缓冲区满（EAGAIN），不算错，保留 POLLOUT 下一轮大循环继续进来喂
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }
            if (errno == EINTR)
            {
                return; // 被系统信号打断，不算错，撤退等下一轮
            }

            // 发生真正的底层管道破裂（如 EPIPE，说明子进程提前崩了拒绝收货）
            std::cerr << "[CGI Writer] Fatal write error on pipe fd " << cgiWriteFd << ", breaking conduit." << std::endl;
            goto ERROR_FUSE;
        }
    }

    // 4. 【大功告成】：判断是否已经全量喂饱子进程？
    if (conn->cgi_body_bytes_sent >= body.size())
    {
    CLOSE_WRITE_Conduit:
        std::cout << "[CGI Writer] Finished feeding all POST body (" << body.size() << " bytes). Closing write pipe." << std::endl;

        // 物理关闭子进程的输入端管道！
        // 这样子进程的标准输入（stdin）就会读到一个完美的 EOF。
        // 绝大多数 CGI 脚本（如 Python 里的 sys.stdin.read()）只有读到 EOF 才会停止挂起、开始疯狂计算并返回结果！
        ::close(cgiWriteFd);
        this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
        this->_cgi_write_fd_to_client_map.erase(it);
        conn->cgi_write_fd = -1;
    }
    return;

ERROR_FUSE:
    // 1. 【管道擦除】：清理写端管道资产与账本
    ::close(cgiWriteFd);
    this->_poll_fds.erase(this->_poll_fds.begin() + poll_idx);
    this->_cgi_write_fd_to_client_map.erase(it);
    conn->cgi_write_fd = -1;

    // 2. 🛡️【连带清场】：顺手解除读端管道的雷达监听（若已挂载），防止悬空孤儿
    if (conn->cgi_read_fd != -1)
    {
        for (size_t j = 0; j < this->_poll_fds.size(); ++j)
        {
            if (this->_poll_fds[j].fd == conn->cgi_read_fd)
            {
                this->_poll_fds.erase(this->_poll_fds.begin() + j);
                break;
            }
        }
        this->_cgi_read_fd_to_client_map.erase(conn->cgi_read_fd);
        ::close(conn->cgi_read_fd);
        conn->cgi_read_fd = -1;
    }

    // 3. 🪓【铁血补枪】：斩杀异常子进程并立刻收尸，坚决不给系统留僵尸！
    if (conn->cgi_pid > 0)
    {
        ::kill(conn->cgi_pid, SIGKILL);
        ::waitpid(conn->cgi_pid, NULL, 0); // 被 SIGKILL 后必定瞬间沦为尸体，物理秒收
        conn->cgi_pid = -1;
    }
    conn->is_cgi = false;

    // 4. 【降维 500】：重装 500 熔断响应，清空旧缓存并灌入新报文
    conn->response.createResponse(500, "CGI Write Pipe Broken", conn->config.error_pages);
    std::string().swap(conn->write_buffer);
    conn->write_buffer = conn->response.responseToString();

    // 5. 【发货拉起】：激活客户端写事件，下一轮 TICK 直接将 500 报错弹射回浏览器！
    this->enableClientWriteEvent(clientFd);
}

/*
函数用途：作为多路复用核心事件分发中枢（Radar 引擎），铁血调度倒序卡尺，将内核弹回的物理事件精准切片并安全分流至各核心车间。
参数与变量：
- activeFd (局部变量)：当前正在接受审查、从全局轮询名册中提取出来的活跃物理文件描述符。
- revents (局部变量)：Linux 内核本次轮询后如实回执的物理就绪事件位掩码（如 POLLIN、POLLOUT、POLLHUP）。
- idx (局部变量)：当前处理的 FD 在 _poll_fds 名册中的物理倒序索引。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的多路复用全局核心轮询监视名册。
实现逻辑：
1. 铁血倒序卡尺防线：采用自后向前的 i-- 倒序遍历机制。完美规避了在分流车间（如 closeConnection）执行动态擦除 vector 元素时，因数据整体前移导致的索引塌陷与 FD 跳过天坑；并就地挂载“物理销户拦截”防御线。
2. 异常/挂断收网分流 (ERR/HUP/NVAL)：
   - 若属于 CGI 管道描述符，即便挂断也必须坚决扭送 handleCgiPipeRead 车间，启动“落幕清仓阶段”，捞干内核缓冲区最后一瓢残渣。
   - 若属于监听套接字（ListenFD）则触发 CRITICAL 警告并安全标记；普通客户端套接字则果断打入 closeConnection 执行销户。
3. 读事件就绪分流 (POLLIN)：
   - CGI 管道读端就绪：分流至 handleCgiPipeRead 启动异步抽水循环。
   - 监听套接字就绪：分流至 acceptNewConnection 物理孵化新进线连接。
   - 普通客户端就绪：分流至 handleClientRead 强攻非阻塞 Socket 捞干内核接收蓄水池。
4. 写事件就绪分流 (POLLOUT)：
   - 在跨过 POLLIN 动作后，原地挂载“生存卡尺防线”，重新校验 idx 与 activeFd 的一致性，严防前置步骤导致的名册动态置换带来的越界血崩。
   - 校验通过后，将 CGI 写端管道精准分流至 handleCgiPipeWrite（卡尺切片喂食 POST Body）；普通客户端分流至 handleClientWrite 执行发件箱一枪弹射发货。
*/
void ServerManager::dispatchEvents()
{
    // 🚀 保持你精妙的倒序遍历
    for (size_t i = this->_poll_fds.size(); i > 0; --i)
    {
        size_t idx = i - 1;

        // 极端防卫：防止在前面几轮循环中已经被物理注销或越界的 FD
        if (idx >= this->_poll_fds.size() || this->_poll_fds[idx].fd == -1 || this->_poll_fds[idx].revents == 0)
            continue;

        int activeFd = this->_poll_fds[idx].fd;
        short revents = this->_poll_fds[idx].revents;

        // ============================================================
        //            在所有业务分流前，原地全量还原最真实的内核世界！
        // ============================================================
        std::cout << "[Radar] --- TICK TRIGGER ---" << std::endl;
        std::cout << "[Radar] FD: " << activeFd
                  << " | idx: " << idx
                  << " | isListen: " << this->isListenFd(activeFd)
                  << " | isCgiPipe: " << this->isCgiPipeFd(activeFd)
                  << " | inConnections: " << (this->_connections.find(activeFd) != this->_connections.end())
                  << " | revents: " << revents
                  << std::endl;

        // ====================  1. 异常事件挂起 (POLLERR / POLLHUP) ====================
        if (revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            if (this->isCgiPipeFd(activeFd))
            {
                std::cout << "[Radar] -> Routing FD " << activeFd << " to handleCgiPipeRead (ERR/HUP)" << std::endl;
                this->handleCgiPipeRead(activeFd, idx);
            }
            else if (this->isListenFd(activeFd))
            {
                std::cerr << "[ServerManager] CRITICAL: Fatal event (" << revents
                          << ") on Listen FD " << activeFd << "!" << std::endl;
                this->_poll_fds[idx].fd = -1;
            }
            else
            {
                std::cout << "[Radar] -> Routing FD " << activeFd << " to closeConnection (ERR/HUP)" << std::endl;
                this->closeConnection(activeFd, idx);
            }
            continue;
        }

        // ====================  2. 读事件就绪 (POLLIN) ====================
        if (revents & POLLIN)
        {
            if (this->isCgiPipeFd(activeFd))
            {
                std::cout << "[Radar] -> Routing FD " << activeFd << " to handleCgiPipeRead (POLLIN)" << std::endl;
                this->handleCgiPipeRead(activeFd, idx);
                continue;
            }
            else if (this->isListenFd(activeFd))
            {
                std::cout << "[Radar] -> Routing FD " << activeFd << " to acceptNewConnection (POLLIN)" << std::endl;
                this->acceptNewConnection(activeFd);
            }
            else
            {
                std::cout << "[Radar] -> Routing FD " << activeFd << " to handleClientRead (POLLIN)" << std::endl;
                this->handleClientRead(activeFd, idx);
            }
        }

        // ==================== 3. 写事件就绪 (POLLOUT) ====================
        if (revents & POLLOUT)
        {
            if (idx >= this->_poll_fds.size() || this->_poll_fds[idx].fd != activeFd)
            {
                std::cout << "[Radar] Notice: FD " << activeFd << " vanished or swapped during POLLIN processing. Safe break." << std::endl;
                continue;
            }

            if (this->isCgiPipeFd(activeFd))
            {
                std::cout << "[Radar] -> Routing FD " << activeFd << " to handleCgiPipeWrite (POLLOUT)" << std::endl;
                this->handleCgiPipeWrite(activeFd, idx);
                continue;
            }
            else
            {
                std::cout << "[Radar] -> Routing FD " << activeFd << " to handleClientWrite (POLLOUT)" << std::endl;
                this->handleClientWrite(activeFd, idx);
            }
        }
        std::cout << "[Radar] --- TICK END for FD " << activeFd << " ---" << std::endl
                  << std::endl;
    }
}

/*
函数用途：物理启动 Webserv 核心主循环大闸（The Matrix），作为永动机总动力引擎，全天候驱动雷达轮询与业务分流。
参数与变量：
- poll_error_retries (局部变量)：int，专门托管给 executePoll 引擎的弹性防线计数器，用于防御系统信号震荡引起的偶发性中断。
- ret (局部变量)：int，每一轮轮询完毕后内核回执的就绪事件总数，若遭遇连续致命崩溃则表现为负数。
- _poll_fds (类内部常驻容器)：std::vector<struct pollfd>，大管家赖以生存的多路复用全局核心轮询监视名册。
实现逻辑：
1. 空舱拦截防线：进线首先点验核心名册。若发现没有任何套接字入籍（_poll_fds 为空），则当场安全熔断折返，严防引发空转血崩。
2. 永动机全天候死循环（while (true)）：
   -第一步（prePollCleanup）：在前置位清理上一轮循环中被标记为脏、死、或已过期的僵尸资产与长连接，保持核心底盘绝对纯净。
   - 第二步（executePoll）：物理拉起多路复用大闸，主线程让出 CPU 陷入非阻塞全天候休眠，并向外部引入弹性防线计数器保护。若返回负数（即连续打断超过 3 次红线），说明遭遇毁灭性灾难，果断强行跳出循环（break）安全撤退。
   - 第三步（dispatchEvents）：若成功捕获有效网络事件，立刻开启倒序卡尺，将事件精准分流至对应的读/写/异常/CGI 业务车间进行最终的弹射交货！
*/
void ServerManager::run()
{
    if (this->_poll_fds.empty())
        return;
    std::cout << "[ServerManager] Main loop started. Entering the matrix..." << std::endl;

    int poll_error_retries = 0;
    while (true)
    {
        this->prePollCleanup();

        int ret = this->executePoll(poll_error_retries);
        if (ret < 0)
            break;

        this->dispatchEvents();
    }
}