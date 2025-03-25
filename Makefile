CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = smallshell
SRC = smallshell.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)