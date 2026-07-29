#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct {
    StatementType type;
} Statement;

typedef struct {
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

InputBuffer* new_input_buffer() {

    InputBuffer* input_buffer = (InputBuffer*)malloc(sizeof(InputBuffer));

    if (input_buffer == NULL) {
        printf("Error: Could not allocate memory for InputBuffer.\n");
        exit(EXIT_FAILURE);
    }

    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;

}

void print_prompt() { printf("db > "); }

void close_input_buffer(InputBuffer* input_buffer) {

    free(input_buffer->buffer);
    free(input_buffer);

}

void read_input(InputBuffer* input_buffer) {

    ssize_t chars_read = getline(&input_buffer -> buffer , &input_buffer->buffer_length, stdin);

     if (chars_read <=0) {
        printf("Error or EOF reached.\n");
        close_input_buffer(input_buffer);
        exit(EXIT_FAILURE);
    }

    if (chars_read > 0 && input_buffer->buffer[chars_read - 1] == '\n') {
        input_buffer->buffer[chars_read - 1] = '\0';
        input_buffer->input_length = chars_read - 1;
    } else {
        input_buffer->input_length = chars_read;
    }

}


int main(int argc, char* argv[]) {
    
    InputBuffer* input_buffer = new_input_buffer();

    while (true) {
        
        print_prompt();
        read_input(input_buffer);

        if (strcmp(input_buffer->buffer, ".exit") == 0) {
            close_input_buffer(input_buffer);
            exit(EXIT_SUCCESS);
        } else {
            printf("Unrecognized keyword '%s'.\n", input_buffer->buffer);
        }
    }


    return 0;
}