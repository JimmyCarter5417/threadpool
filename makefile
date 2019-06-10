CC        = gcc
INCLUDE   = 
LINKPARAM = -lpthread
CFLAGS    = -Wall -Wextra -g -O0
SRC       = $(wildcard *.c)
OBJ       = $(SRC:.c=.o)
PROGRAM   = test

all: clean $(PROGRAM)

$(PROGRAM): $(OBJ)
	$(CC) $(OBJ) $(LINKPARAM) -o $@

clean :
	rm -f *.o
	rm -f $(PROGRAM)
    
%.o : %.c
	$(CC) $(INCLUDE) $(CFLAGS) -c $< -o $@

.PHONY : clean
