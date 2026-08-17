
NAME		=	webserv

# Compilador
CXX			=	c++
CXXFLAGS	=	-Wall -Wextra -Werror -std=c++98 -MMD -MP

# Directorios
SRC_DIR		=	src
OBJ_DIR		=	obj
INC_DIRS	=	$(SRC_DIR) 				$(SRC_DIR)/cgi-includes 				$(SRC_DIR)/request 				$(SRC_DIR)/response 				$(SRC_DIR)/sockets-includes

# Includes con -I
INCLUDES	=	$(addprefix -I, $(INC_DIRS))

# Archivos fuente
SRCS		=	$(SRC_DIR)/main.cpp 				$(SRC_DIR)/sockets/socket.cpp 				$(SRC_DIR)/request/parseInputRequest.cpp 				$(SRC_DIR)/response/parseResponse.cpp 				$(SRC_DIR)/cgi/parserCgi.cpp

# Archivos objeto
OBJS		=	$(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
DEPS		=	$(OBJS:.o=.d)

# Colores
GREEN		=	␛[0;32m
YELLOW		=	␛[0;33m
RED			=	␛[0;31m
BLUE		=	␛[0;34m
RESET		=	␛[0m

# Reglas
all:
		@mkdir -p $(OBJ_DIR)/sockets $(OBJ_DIR)/request $(OBJ_DIR)/response $(OBJ_DIR)/cgi
		@$(MAKE) $(NAME)

$(NAME): $(OBJS)
		@echo "$(YELLOW)Linkando $(NAME)...$(RESET)"
		$(CXX) $(CXXFLAGS) $(INCLUDES) $(OBJS) -o $(NAME)
		@echo "$(GREEN)✓ $(NAME) compilado con éxito!$(RESET)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
		@echo "$(BLUE)Compilando $<...$(RESET)"
		@mkdir -p $(dir $@)
		$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Incluir dependencias generadas
-include $(DEPS)

clean:
		@echo "$(RED)Limpiando objetos...$(RESET)"
		@rm -rf $(OBJ_DIR)

fclean: clean
		@echo "$(RED)Limpiando ejecutable...$(RESET)"
		@rm -f $(NAME)

re: fclean all

# Ejecutar el servidor
run: all
		./$(NAME)

# Debug con flags adicionales
debug: CXXFLAGS += -g -fsanitize=address
debug: re

.PHONY: all clean fclean re run debug
