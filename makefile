NAME		=	webserv

CXX			=	c++
CXXFLAGS	=	-Wall -Wextra -Werror -std=c++98 -MMD -MP -no-pie

SRC_DIR		=	src
OBJ_DIR		=	obj
INC_DIRS	=	$(SRC_DIR) 				$(SRC_DIR)/cgi-includes 				$(SRC_DIR)/request 				$(SRC_DIR)/response 				$(SRC_DIR)/sockets-includes 				$(SRC_DIR)/confile

INCLUDES	=	$(addprefix -I, $(INC_DIRS))

SRCS		=	$(SRC_DIR)/main.cpp 				$(SRC_DIR)/sockets/socket.cpp 				$(SRC_DIR)/request/parseInputRequest.cpp 				$(SRC_DIR)/response/parseResponse.cpp 				$(SRC_DIR)/cgi/parserCgi.cpp 				$(SRC_DIR)/confile/configurationFileParse.cpp

OBJS		=	$(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
DEPS		=	$(OBJS:.o=.d)

GREEN		=	\033[0;32m
YELLOW		=	\033[0;33m
RED			=	\033[0;31m
BLUE		=	\033[0;34m
RESET		=	\033[0m

all:
		@mkdir -p $(OBJ_DIR)/sockets $(OBJ_DIR)/request $(OBJ_DIR)/response $(OBJ_DIR)/cgi
		@$(MAKE) $(NAME)

$(NAME): $(OBJS)
		@printf "$(YELLOW)Linkando $(NAME)...$(RESET)\n"
		$(CXX) $(CXXFLAGS) $(INCLUDES) $(OBJS) -o $(NAME)
		@printf "$(GREEN)✓ $(NAME) compilado con éxito!$(RESET)\n"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
		@printf "$(BLUE)Compilando $<...$(RESET)\n"
		@mkdir -p $(dir $@)
		$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

-include $(DEPS)

clean:
		@printf "$(RED)Limpiando objetos...$(RESET)\n"
		@rm -rf $(OBJ_DIR)

fclean: clean
		@printf "$(RED)Limpiando ejecutable...$(RESET)\n"
		@rm -f $(NAME)

re: fclean all

run: all
		./$(NAME)

debug: CXXFLAGS += -g -fsanitize=address
debug: re

.PHONY: all clean fclean re run debug
