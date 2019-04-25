CC     = gcc
CFLAGS = -Wall -Wextra -g -O0
LIBS   = -lpthread
SRC    = *.c
TARGET = test

all : $(SRC)
	$(CC) $(SRC) $(CFLAGS) $(LIBS) -o $(TARGET)
clean : 
	rm -f $(TARGET)

.PHONY: clean
