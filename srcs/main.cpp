#include "Webserv.hpp"  // 🟢 完美！没有相对路径，没有文件夹前缀，注意W大写！

int main(int argc, char **argv)
{
    std::string config_path = "default.conf"; // 默认配置文件路径
    if (argc > 1)
        config_path = argv[1]; // 如果用户提供了配置文件路径，则使用它

    Config config(config_path); // 创建 Config 对象，解析配置文件

    if (config.error) // 检查配置解析是否有错误
    {
        std::cerr << "Configuration error. Exiting." << std::endl;
        return 1; // 配置有误，退出程序
    }

    // 继续执行其他逻辑，例如 setupSockets() 和 serverLoop()
    return 0; // 正常退出
}