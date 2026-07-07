NAME = webserv

CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98

# 🟢 维持你们团队多业务模块的物理目录阵列
SRC_DIRS = \
    srcs \
    srcs/Config \
    srcs/Server \
    srcs/Request \
    srcs/Response \
    srcs/Cgi \
    srcs/Bonus \
    srcs/Utils

SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp))
        
OBJS = $(SRCS:.cpp=.o)

# 🚀 【神级暴改：全纵深物理路径平坦化网关】
# 解释：1. -I includes 确保全局任何地方都能通过 #include "webserv.hpp" 抓到总头文件。
#       2. $(patsubst %, -I %, $(SRC_DIRS)) 极其关键！
#          它会把 srcs 加上它下面所有的 Config、Server 等子目录全部自动化套上 -I 喂给编译器。
#          这样，编译器在解析任何头文件（包括 includes/webserv.hpp）时，
#          去 srcs 的各个子肚子里捞文件就像在自己家客厅一样自由，压根不需要相对路径！
INCLUDES = -I includes $(patsubst %, -I %, $(SRC_DIRS))

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re