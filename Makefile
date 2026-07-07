NAME = webserv

CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98

SRC_DIRS = \
	srcs \
	srcs/Config \
	srcs/Server \
	srcs/Request \
	srcs/Response \
	srcs/Cgi \
	srcs/Bonus \
	srcs/Utils

OBJ_DIR = obj

SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp))

# 将 srcs/xxx.cpp -> obj/srcs/xxx.o
OBJS = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRCS))

INCLUDES = -I includes $(patsubst %, -I %, $(SRC_DIRS))

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

# 编译规则
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re