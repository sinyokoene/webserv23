CC = c++
CFLAGS = -Wall -Wextra -Werror -std=c++98 -Iinclude
SRC = src/main.cpp src/ConfigParser.cpp src/HttpRequest.cpp src/HttpResponse.cpp src/Server.cpp src/ServerHandlers.cpp src/LocationConfig.cpp src/Socket.cpp src/Utils.cpp src/ServerCgiHandler.cpp
OBJ = $(SRC:.cpp=.o)
NAME = webserv

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(OBJ) -o $(NAME)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re