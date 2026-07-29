#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>

typedef enum{
    COMMAND_METACOMMAND,
    COMMAND_STATEMENT
} CommandType;

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


void close_input_buffer(InputBuffer* input_buffer) {

    free(input_buffer->buffer);
    free(input_buffer);

}

void print_prompt() { printf("db > "); }

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

MetaCommandResult prepare_meta_command(InputBuffer* input_buffer){
    

    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        close_input_buffer(input_buffer);
        exit(EXIT_SUCCESS);
    }
    
    return META_COMMAND_UNRECOGNIZED_COMMAND ;
    
}

void do_meta_command(InputBuffer* input_buffer){
    switch (prepare_meta_command(input_buffer)) {
        case META_COMMAND_SUCCESS:
            break;
        
        case META_COMMAND_UNRECOGNIZED_COMMAND : 
            printf("META_COMMAND_UNRECOGNIZED_COMMAND\n");
            break;
    }

}


PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement){

    if(strncmp(input_buffer->buffer, "insert", 6)){
        statement->type = STATEMENT_INSERT;
        return PREPARE_SUCCESS;
    }

    if(strcmp(input_buffer->buffer, "select")) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

void execute_statement(Statement* statement) {

    switch (statement->type) {
        case STATEMENT_SELECT : 
            printf("SELECT\n");
            break;
        
        case STATEMENT_INSERT : 
            printf("INSERT\n");
            break;
    }
}

void do_statement(InputBuffer* input_buffer){
    Statement statement;
    PrepareResult prepare_result = prepare_statement(input_buffer, &statement);

    if(prepare_result==PREPARE_SUCCESS) execute_statement(&statement);
    else printf("PREPARE_UNRECOGNIZED_STATEMENT\n");
}

CommandType find_command_type(InputBuffer* input_buffer){
    if(input_buffer->buffer[0] == '.') return COMMAND_METACOMMAND;

    return COMMAND_STATEMENT;
}

int main(int argc, char* argv[]) {
    
    InputBuffer* input_buffer = new_input_buffer();

    while (true) {

        print_prompt();
        read_input(input_buffer);

        CommandType command_type = find_command_type(input_buffer);


        switch(command_type){
            case COMMAND_METACOMMAND : 
                do_meta_command(input_buffer);
                break;
            case COMMAND_STATEMENT : 
                do_statement(input_buffer);
                break;
        }
        


    }

    return 0;
}