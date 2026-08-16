#include "Webserv.hpp"

// Starts the web server program.
int main(int argc, char **argv)
{
    std::string config_path = "default.conf";
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
    ServerManager srvmng(config.getServers());
    srvmng.init();
    srvmng.run();
    return 0;
}
