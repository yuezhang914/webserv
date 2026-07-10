#include "Webserv.hpp" 

/**
 * 函数：main
 * 用途：整个 Webserv 高并发多路复用 Web 服务器的物理启动大入口，负责参数校验、配置加载、资产拉起及核心引擎的持续驱动。
 * 参数来源：
 *     - argc：命令行传入的参数总个数。
 *     - argv：命令行传入的参数字符串数组，argv[1] 预期为自定义配置文件路径。
 * 变量解释：
 *     - config_path：字符串，存放待解析的配置文件路径，缺省默认为 "default.conf"。
 *     - config：Config 类的实体对象，负责在构造时对配置文件执行 Token 词法切分与合规验证。
 *     - srvmng：ServerManager 底层大管家实例，负责将解析好的虚拟主机账本转换为真实的内核套接字。
 * 实现逻辑：
 *     1. 严防死守：核验参数个数 argc。若 argc 大于 2，说明用户传入了杂乱的冗余参数，直接打印 Usage 规范并熔断退出。
 *     2. 路由指派：若 argc 等于 2，说明用户指明了自定义配置，将 argv[1] 赋予 config_path。
 *     3. 冷启动解析：实例化 Config 对象。一旦对象内部的 config.error 旗帜被挂起（包含语法错、漏 root、或学姐绝杀的同端口同域名冲突），立马无情报错并中断进程。
 *     4. 资产核验：打印配置账本进行视觉调试，利用 config.getServers() 将清洗干净的数据喂给 ServerManager。
 *     5. 物理铺路：调用 srvmng.setupSockets() 砸开网卡端口，绑定地址并设置为非阻塞。
 *     6. 引擎点火：调用 srvmng.run() 挺进永不复返的高并发多路复用 poll() 大循环。
 * 后续影响：主线程自此常驻内核事件等待队列，服务器开始全面接受外部客户端的 HTTP 洪流冲击。
 */
int main(int argc, char **argv)
{ 
    signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, signalHandler);
    std::string config_path = "default.conf"; // 默认配置文件路径

    if (argc > 2)
    {
        std::cerr << "Usage: " << argv[0] << " [config_file_path]" << std::endl;
        return 1;
    }


    if (argc == 2)
    {
        config_path = argv[1]; 
    }


    Config config(config_path); 

  
    if (config.error) 
    {
        std::cerr << "Critical Error: Configuration initialization failed. Exiting." << std::endl;
        return 1; 
    }

    config.printConfig(); 

    ServerManager srvmng(config.getServers());
    
    
    srvmng.init();
    
    
    srvmng.run();

    return 0; 
}