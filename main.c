#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

#define ID_SIZE          sizeof(((Row*)0)->id)          // 4 byte
#define USERNAME_SIZE    sizeof(((Row*)0)->username)    // 33 byte
#define EMAIL_SIZE       sizeof(((Row*)0)->email)       // 256 byte

#define ID_OFFSET        0
#define USERNAME_OFFSET  (ID_OFFSET + ID_SIZE)          // 4
#define EMAIL_OFFSET     (USERNAME_OFFSET + USERNAME_SIZE) // 37
#define ROW_SIZE         (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE) // 293 byte

#define PAGE_SIZE 4096
#define TABLE_MAX_PAGES 100

typedef struct {
    int file_descriptor;
    uint32_t file_length;
    void* pages[TABLE_MAX_PAGES];
} Pager;


typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;

const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE; // 13
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES; // 1300

typedef struct {
    uint32_t num_rows;
    void* pages[TABLE_MAX_PAGES]; 
    Pager* pager;
} Table;

typedef struct {
    Table* table;
    uint32_t row_num;
    bool end_of_table;
} Cursor;

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

typedef struct {
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct {
    StatementType type;
    Row row_to_insert;
} Statement;

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL
} ExecuteResult;



void* get_page(Pager* pager, uint32_t page_num) {
  if (page_num >= TABLE_MAX_PAGES) {
    printf("Tried to fetch page number out of bounds. %d > %d\n", page_num,
           TABLE_MAX_PAGES);
    exit(EXIT_FAILURE);
  }

  if (pager->pages[page_num] == NULL) {
    // Cache miss. Allocate memory and load from file.
    void* page = malloc(PAGE_SIZE);
    uint32_t num_pages = pager->file_length / PAGE_SIZE;

    // We might save a partial page at the end of the file
    if (pager->file_length % PAGE_SIZE) {
      num_pages += 1;
    }

    if (page_num <= num_pages) {
      lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
      ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
      if (bytes_read == -1) {
        printf("Error reading file: %d\n", errno);
        exit(EXIT_FAILURE);
      }
    }

    pager->pages[page_num] = page;
  }

  return pager->pages[page_num];
}

void serialize_row(Row* source, void* destination) {
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void deserialize_row(void* source, Row* destination) {
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

void print_row(Row* row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

#define ROWS_PER_PAGE 13

void* row_slot(Table* table, uint32_t row_num) {

    uint32_t page_num = row_num / ROWS_PER_PAGE;

    void* page = table->pages[page_num];
    if (page == NULL)  page = table->pages[page_num] = malloc(PAGE_SIZE);
    
    uint32_t row_offset = row_num % ROWS_PER_PAGE;
    uint32_t byte_offset = row_offset * ROW_SIZE;
    
    return (uint8_t*)page + byte_offset;
}

Table* new_table() {
    Table* table = (Table*)malloc(sizeof(Table));
    table->num_rows = 0;
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        table->pages[i] = NULL;
    }
    return table;
}

void free_table(Table* table) {
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        if (table->pages[i] != NULL) {
            free(table->pages[i]);
        }
    }
    free(table);
}

Cursor* table_start(Table* table) {

    Cursor* cursor = malloc(sizeof(Cursor));

    cursor->table = table;
    cursor->row_num = 0;
    cursor->end_of_table = (table->num_rows == 0);

    return cursor;
}

Cursor* table_end(Table* table) {

    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->row_num = table->num_rows;
    cursor->end_of_table = true;
    return cursor;
}


void* cursor_value(Cursor* cursor) {

    uint32_t page_num = cursor->row_num / ROWS_PER_PAGE;
    void* page = get_page(cursor->table->pager, page_num);
    uint32_t row_offset = cursor->row_num % ROWS_PER_PAGE;

    return (uint8_t*)page + (row_offset * ROW_SIZE) ;
}

void cursor_advance(Cursor* cursor) {

    cursor->row_num++;
    if(cursor->row_num >= cursor->table->num_rows) cursor->end_of_table = true;
}




Pager* pager_open(const char* filename) {
  int fd = open(filename,
                O_RDWR | O_CREAT,  // Read/Write mode, Create file if it doesn't exist
                S_IWUSR | S_IRUSR); // User write permission, User read permission

  if (fd == -1) {
    printf("Unable to open file\n");
    exit(EXIT_FAILURE);
  }

  off_t file_length = lseek(fd, 0, SEEK_END);

  Pager* pager = malloc(sizeof(Pager));
  pager->file_descriptor = fd;
  pager->file_length = file_length;

  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    pager->pages[i] = NULL;
  }

  return pager;
}

void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
  if (pager->pages[page_num] == NULL) {
    printf("Tried to flush null page\n");
    exit(EXIT_FAILURE);
  }

  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);

  if (offset == -1) {
    printf("Error seeking: %d\n", errno);
    exit(EXIT_FAILURE);
  }

  ssize_t bytes_written =
      write(pager->file_descriptor, pager->pages[page_num], size);

  if (bytes_written == -1) {
    printf("Error writing: %d\n", errno);
    exit(EXIT_FAILURE);
  }
}

Table* db_open(const char* filename) {
    Pager* pager = pager_open(filename);

    Table* table = malloc(sizeof(Table));
    table->pager = pager;
    
    table->num_rows = pager->file_length / ROW_SIZE;

    return table;
}

void db_close(Table* table) {
    Pager* pager = table->pager;
    uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;

    for (uint32_t i = 0; i < num_full_pages; i++) {
        if (pager->pages[i] == NULL) {
            continue;
        }
        pager_flush(pager, i, PAGE_SIZE);
        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }

    uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
    if (num_additional_rows > 0) {
        uint32_t page_num = num_full_pages;
        if (pager->pages[page_num] != NULL) {
            pager_flush(pager, page_num, num_additional_rows * ROW_SIZE); 
            free(pager->pages[page_num]);
            pager->pages[page_num] = NULL;
        }
    }

    int result = close(pager->file_descriptor);
    if (result == -1) {
        printf("Error closing db file.\n");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        void* page = pager->pages[i];
        if (page) {
            free(page);
            pager->pages[i] = NULL;
        }
    }

    free(pager);
    free(table);
}

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

void read_input(InputBuffer* input_buffer,Table* table) {

    ssize_t chars_read = getline(&input_buffer -> buffer , &input_buffer->buffer_length, stdin);

     if (chars_read <=0) {
        printf("Error or EOF reached.\n");
        close_input_buffer(input_buffer);
        db_close(table);
        exit(EXIT_FAILURE);
    }

    if (chars_read > 0 && input_buffer->buffer[chars_read - 1] == '\n') {
        input_buffer->buffer[chars_read - 1] = '\0';
        input_buffer->input_length = chars_read - 1;
    } else {
        input_buffer->input_length = chars_read;
    }

}

MetaCommandResult prepare_meta_command(InputBuffer* input_buffer,Table* table){
    

    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        close_input_buffer(input_buffer);
        db_close(table);
        exit(EXIT_SUCCESS);
    }
    
    return META_COMMAND_UNRECOGNIZED_COMMAND ;
    
}

void do_meta_command(InputBuffer* input_buffer,Table* table){
    switch (prepare_meta_command(input_buffer,table)) {
        case META_COMMAND_SUCCESS:
            break;
        
        case META_COMMAND_UNRECOGNIZED_COMMAND : 
            printf("META_COMMAND_UNRECOGNIZED_COMMAND\n");
            break;
    }

}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement){

    if(strncmp(input_buffer->buffer, "insert", 6) == 0 ){
    statement->type = STATEMENT_INSERT;

    strtok(input_buffer->buffer, " ");
    char* id_string = strtok(NULL, " ");
    char* username = strtok(NULL, " ");
    char* email = strtok(NULL, " ");

    if (id_string == NULL || username == NULL || email == NULL) {
        return PREPARE_UNRECOGNIZED_STATEMENT;
    }

    int id = atoi(id_string);
    if (id < 0) {
        return PREPARE_UNRECOGNIZED_STATEMENT;
    }

    if (strlen(username) > COLUMN_USERNAME_SIZE) {
        return PREPARE_UNRECOGNIZED_STATEMENT;
    }
    if (strlen(email) > COLUMN_EMAIL_SIZE) {
        return PREPARE_UNRECOGNIZED_STATEMENT;
    }

    statement->row_to_insert.id = id;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARE_SUCCESS;
}

    if(strcmp(input_buffer->buffer, "select") == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

ExecuteResult execute_insert(Statement* statement, Table* table) {
    if (table->num_rows >= TABLE_MAX_ROWS) {
        return EXECUTE_TABLE_FULL;
    }

    Row* row_to_insert = &(statement->row_to_insert);
    Cursor* cursor = table_end(table);
    void* destination = cursor_value(cursor);
    serialize_row(row_to_insert, destination);   

    table->num_rows++;
    free(cursor);

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Table* table) {

    Cursor* cursor = table_start(table);

    while(!cursor->end_of_table){
        Row row;
        deserialize_row(cursor_value(cursor), &row);
        print_row(&row);
        cursor_advance(cursor);
    }

    free(cursor);
    return EXECUTE_SUCCESS;
}

void execute_statement(Statement* statement,Table* table) {

    switch (statement->type) {
        case STATEMENT_SELECT : 
            execute_select(table);
            break;
        
        case STATEMENT_INSERT : 
                if (execute_insert(statement, table) == EXECUTE_TABLE_FULL)  printf("Error: Table full.\n");
                break;
    }
}

void do_statement(InputBuffer* input_buffer, Table* table){
    Statement statement;
    PrepareResult prepare_result = prepare_statement(input_buffer, &statement);

    if(prepare_result==PREPARE_SUCCESS) execute_statement(&statement,table);
    else printf("PREPARE_UNRECOGNIZED_STATEMENT\n");
}

CommandType find_command_type(InputBuffer* input_buffer){
    if(input_buffer->buffer[0] == '.') return COMMAND_METACOMMAND;

    return COMMAND_STATEMENT;
}

int main(int argc, char* argv[]) {
    
    if (argc < 2) {
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }

    char* filename = argv[1];
    Table* table = db_open(filename);
    InputBuffer* input_buffer = new_input_buffer();

    while (true) {

        print_prompt();
        fflush(stdout);
        read_input(input_buffer,table);

        CommandType command_type = find_command_type(input_buffer);


        switch(command_type){
            case COMMAND_METACOMMAND : 
                do_meta_command(input_buffer,table);
                break;
            case COMMAND_STATEMENT : 
                do_statement(input_buffer,table);
                break;
        }
        


    }

    return 0;
}