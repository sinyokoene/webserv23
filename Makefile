CC = c++
CFLAGS = -Wall -Wextra -Werror -std=c++98 -Iinclude
SRC = src/main.cpp src/ConfigParser.cpp src/HttpRequest.cpp src/HttpResponse.cpp src/Server.cpp src/ServerHandlers.cpp src/LocationConfig.cpp src/Utils.cpp src/ServerCgiHandler.cpp
OBJ_DIR = obj
OBJ = $(SRC:src/%.cpp=$(OBJ_DIR)/%.o)
NAME = webserv

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(OBJ) -o $(NAME)

$(OBJ_DIR)/%.o: src/%.cpp | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
