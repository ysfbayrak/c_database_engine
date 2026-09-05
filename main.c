#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* ============================================================================
 *                       CONSTANTS, ENUMS & DATA LAYOUTS                      *
 * ============================================================================ */

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;

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
    uint32_t num_pages;
    void* pages[TABLE_MAX_PAGES];
} Pager;

typedef struct {
    uint32_t root_page_num;
    Pager* pager;
} Table;

typedef struct {
    Table* table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table;
} Cursor;

typedef enum {
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
    EXECUTE_TABLE_FULL,
    EXECUTE_DUPLICATE_KEY
} ExecuteResult;

typedef enum {
    NODE_INTERNAL,
    NODE_LEAF
} NodeType;

const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;

const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = sizeof(uint8_t);

const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = sizeof(uint8_t) + sizeof(uint8_t);

const uint8_t COMMON_NODE_HEADER_SIZE =
    sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t);

const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET =
    sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t);

const uint32_t LEAF_NODE_HEADER_SIZE =
    sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t);

const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;

const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET =
    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;

const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS = LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;

/* ============================================================================
 *                        DISK & PAGE MANAGEMENT (PAGER)                      *
 * ============================================================================ */

Pager* pager_open(const char* filename) {
    int fd = open(filename,
                  O_RDWR | O_CREAT,
                  S_IWUSR | S_IRUSR);

    if (fd == -1) {
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(fd, 0, SEEK_END);

    Pager* pager = malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->file_length = file_length;
    pager->num_pages = (file_length / PAGE_SIZE);

    if (file_length % PAGE_SIZE != 0) {
        printf("Db file is not a whole number of pages. Corrupt file.\n");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }

    return pager;
}

void pager_flush(Pager* pager, uint32_t page_num) {
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
        write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);

    if (bytes_written == -1) {
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

void* get_page(Pager* pager, uint32_t page_num) {
    if (page_num >= TABLE_MAX_PAGES) {
        printf("Tried to fetch page number out of bounds. %d > %d\n", page_num,
               TABLE_MAX_PAGES);
        exit(EXIT_FAILURE);
    }

    if (pager->pages[page_num] == NULL) {
        void* page = malloc(PAGE_SIZE);
        uint32_t num_pages = pager->file_length / PAGE_SIZE;

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

        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        } 
    }

    return pager->pages[page_num];
}

/* ============================================================================
 *                          ROW & NODE MEMORY HELPERS                         *
 * ============================================================================ */

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

uint32_t* leaf_node_num_cells(void* node) {
    return (uint32_t*)((uint8_t*)node + LEAF_NODE_NUM_CELLS_OFFSET);
}

void* leaf_node_cell(void* node, uint32_t cell_num) {
    return (uint8_t*)node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

void* leaf_node_key(void* node, uint32_t cell_num) {
    return leaf_node_cell(node, cell_num);
}

void* leaf_node_value(void* node, uint32_t cell_num) {
    return (uint8_t*)leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

void initialize_leaf_node(void* node) { 
    *leaf_node_num_cells(node) = 0; 
}

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = get_page(cursor->table->pager, cursor->page_num);

    uint32_t num_cells = *leaf_node_num_cells(node);
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        printf("Need to implement splitting a leaf node.\n");
        exit(EXIT_FAILURE);
    }

    if (cursor->cell_num < num_cells) {
        void* destination = leaf_node_cell(node, cursor->cell_num + 1);
        void* source = leaf_node_cell(node, cursor->cell_num);
        size_t bytes_to_move = (num_cells - cursor->cell_num) * LEAF_NODE_CELL_SIZE;

        memmove(destination, source, bytes_to_move);
    }

    *(leaf_node_num_cells(node)) += 1;
    *(uint32_t*)leaf_node_key(node, cursor->cell_num) = key;
    serialize_row(value, leaf_node_value(node, cursor->cell_num));
}

/* ============================================================================
 *                          TABLE, CURSOR & SEARCH                            *
 * ============================================================================ */

Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
    void* node = get_page(table->pager, page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = page_num;

    // Binary search
    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_cells;
    while (one_past_max_index != min_index) {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *(uint32_t*)leaf_node_key(node, index);
        if (key == key_at_index) {
            cursor->cell_num = index;
            return cursor;
        }
        if (key < key_at_index) {
            one_past_max_index = index;
        } else {
            min_index = index + 1;
        }
    }

    cursor->cell_num = min_index;
    return cursor;
}

Cursor* table_find(Table* table, uint32_t key) {
    uint32_t root_page_num = table->root_page_num;
    return leaf_node_find(table, root_page_num, key);
}

Cursor* table_start(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = table->root_page_num;
    cursor->cell_num = 0;

    void* root_node = get_page(table->pager, table->root_page_num);
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    cursor->end_of_table = (num_cells == 0);

    return cursor;
}

void* cursor_value(Cursor* cursor) {
    uint32_t page_num = cursor->page_num;
    void* page = get_page(cursor->table->pager, page_num);
    return leaf_node_value(page, cursor->cell_num);
}

void cursor_advance(Cursor* cursor) {
    uint32_t page_num = cursor->page_num;
    void* node = get_page(cursor->table->pager, page_num);

    cursor->cell_num += 1;
    if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
        cursor->end_of_table = true;
    }
}

Table* db_open(const char* filename) {
    Pager* pager = pager_open(filename);
    Table* table = malloc(sizeof(Table));
    table->pager = pager;
    table->root_page_num = 0;

    if (pager->num_pages == 0) {
        void* root_node = get_page(pager, 0);
        initialize_leaf_node(root_node);
    }

    return table;
}

void db_close(Table* table) {
    Pager* pager = table->pager;

    for (uint32_t i = 0; i < pager->num_pages; i++) {
        if (pager->pages[i] == NULL) {
            continue;
        }
        pager_flush(pager, i);
        free(pager->pages[i]);
        pager->pages[i] = NULL;
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

/* ============================================================================
 *                            QUERY EXECUTION                                 *
 * ============================================================================ */

ExecuteResult execute_insert(Statement* statement, Table* table) {
    void* node = get_page(table->pager, table->root_page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    Row* row_to_insert = &(statement->row_to_insert);
    uint32_t key_to_insert = row_to_insert->id;
    Cursor* cursor = table_find(table, key_to_insert);

    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *(uint32_t*)leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key_to_insert) {
            free(cursor);
            return EXECUTE_DUPLICATE_KEY;
        }
    }

    leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

    free(cursor);
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Table* table) {
    Cursor* cursor = table_start(table);

    while (!cursor->end_of_table) {
        Row row;
        deserialize_row(cursor_value(cursor), &row);
        print_row(&row);
        cursor_advance(cursor);
    }

    free(cursor);
    return EXECUTE_SUCCESS;
}

void execute_statement(Statement* statement, Table* table) {
    switch (statement->type) {
        case STATEMENT_SELECT:
            execute_select(table);
            break;
        case STATEMENT_INSERT:
            switch (execute_insert(statement, table)) {
                case EXECUTE_SUCCESS:
                    printf("Executed.\n");
                    break;
                case EXECUTE_DUPLICATE_KEY:
                    printf("Error: Duplicate key.\n");
                    break;
                case EXECUTE_TABLE_FULL:
                    printf("Error: Table full.\n");
                    break;
            }
            break;
    }
}

/* ============================================================================
 *                          INPUT BUFFER, REPL & PARSER                       *
 * ============================================================================ */

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

void print_prompt() { 
    printf("db > "); 
}

void read_input(InputBuffer* input_buffer, Table* table) {
    ssize_t chars_read = getline(&input_buffer->buffer, &input_buffer->buffer_length, stdin);

    if (chars_read <= 0) {
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

MetaCommandResult prepare_meta_command(InputBuffer* input_buffer, Table* table) {
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        close_input_buffer(input_buffer);
        db_close(table);
        exit(EXIT_SUCCESS);
    }
    return META_COMMAND_UNRECOGNIZED_COMMAND;
}

void do_meta_command(InputBuffer* input_buffer, Table* table) {
    switch (prepare_meta_command(input_buffer, table)) {
        case META_COMMAND_SUCCESS:
            break;
        case META_COMMAND_UNRECOGNIZED_COMMAND:
            printf("Unrecognized command '%s'\n", input_buffer->buffer);
            break;
    }
}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement) {
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        statement->type = STATEMENT_INSERT;

        char* keyword = strtok(input_buffer->buffer, " ");
        (void)keyword;
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

    if (strcmp(input_buffer->buffer, "select") == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

void do_statement(InputBuffer* input_buffer, Table* table) {
    Statement statement;
    PrepareResult prepare_result = prepare_statement(input_buffer, &statement);

    if (prepare_result == PREPARE_SUCCESS) {
        execute_statement(&statement, table);
    } else {
        printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
    }
}

CommandType find_command_type(InputBuffer* input_buffer) {
    if (input_buffer->buffer[0] == '.') return COMMAND_METACOMMAND;
    return COMMAND_STATEMENT;
}

/* ============================================================================
 *                                 ENTRY POINT                                *
 * ============================================================================ */

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
        read_input(input_buffer, table);

        CommandType command_type = find_command_type(input_buffer);

        switch (command_type) {
            case COMMAND_METACOMMAND:
                do_meta_command(input_buffer, table);
                break;
            case COMMAND_STATEMENT:
                do_statement(input_buffer, table);
                break;
        }
    }

    return 0;
}