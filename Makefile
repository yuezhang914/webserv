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

SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp))
		
OBJS = $(SRCS:.cpp=.o)

INCLUDES = -I includes

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