#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAX_PATH_LENGTH 256
#define CLUSTER_SIZE 4096
#define MAX_CLUSTERS 4096
#define FAT_FREE 0
#define FAT_END (-1)

int fat[MAX_CLUSTERS];

// Pseudo FAT structure (simplified for the task)
typedef struct {
    char filename[MAX_PATH_LENGTH];
    size_t size;
    size_t start_cluster;
    size_t end_cluster;
    int is_directory;
} FileEntry;

typedef struct {
    const char *command_name;
    void (*command_func)(const char *);
} Command;

void cp(const char *arg1, const char *arg2);
void mv(const char *arg1, const char *arg2);
void rm(const char *arg1);
void create_directory(const char *arg);
int remove_directory(const char *arg);
void ls(const char *arg);
void cat(const char *arg);
void cd(const char *arg);
void pwd();
void info(const char *arg);
void incp(const char *arg1, const char *arg2);
void outcp(const char *arg1, const char *arg2);
void format(size_t size_mb, const char *filename);
void load(const char *filename);
void normalize_filesystem_paths();
void bug();
void check();

void remove_directory_wrapper(const char *arg) {
    remove_directory(arg); // Вызов оригинальной функции с адаптированным аргументом
}


Command command_table[] = {
    {"cp", (void (*)(const char *))cp},
    {"mv", (void (*)(const char *))mv},
    {"rm", rm},
    {"create_directory", create_directory},
    {"remove_directory", remove_directory_wrapper}, // Используем обёртку
    {"ls", ls},
    {"cat", cat},
    {"cd", cd},
    {"pwd", (void (*)(const char *))pwd}, // Преобразуем `void (*)()` в `void (*)(const char *)`
    {"info", info},
    {"incp", (void (*)(const char *))incp},
    {"outcp", (void (*)(const char *))outcp},
    {"format", (void (*)(const char *))format},
    {"load", load},
    {"bug", bug},     // Добавляем команду bug
    {"check", check}  // Добавляем команду check
};

// Simulated pseudo-FAT file system metadata
#define MAX_FILES 100
FileEntry filesystem[MAX_FILES];
size_t file_count = 0;
char current_path[MAX_PATH_LENGTH] = "/";

void initialize_fat() {
    for (int i = 0; i < MAX_CLUSTERS; i++) {
        fat[i] = FAT_FREE; // Все кластеры свободны
    }
}

// Mock file system initialization
void initialize_filesystem() {
    file_count = 0;
    memset(filesystem, 0, sizeof(filesystem));
    strcpy(current_path, "/");
    initialize_fat();
}

int allocate_cluster(FileEntry *file_entry) {
    size_t clusters_needed = (file_entry->size + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
    int first_cluster = -1;
    file_entry->end_cluster = -1; // Сброс end_cluster перед началом

    for (int i = 0; i < MAX_CLUSTERS && clusters_needed > 0; i++) {
        if (fat[i] == FAT_FREE) {
            if (first_cluster == -1) {
                first_cluster = i; // Первый кластер
                file_entry->start_cluster = first_cluster;
            }

            if (file_entry->end_cluster != -1) {
                fat[file_entry->end_cluster] = i; // Устанавливаем связь
            }

            file_entry->end_cluster = i; // Обновляем end_cluster
            fat[i] = FAT_END;            // Помечаем как конец
            clusters_needed--;
        }
    }

    // Если остались невыделенные кластеры
    if (clusters_needed > 0) {
        printf("NO FREE CLUSTERS\n");

        // Освобождаем уже выделенные кластеры
        int current = first_cluster;
        while (current != -1 && current != FAT_END) {
            int next = fat[current];
            fat[current] = FAT_FREE;
            current = next;
        }

        file_entry->start_cluster = FAT_FREE; // Сбрасываем start_cluster
        file_entry->end_cluster = FAT_FREE;   // Сбрасываем end_cluster
        return -1;
    }

    return first_cluster;
}

// Utility to find a file by name in the pseudo filesystem
int find_file(const char *filename) {
    for (size_t i = 0; i < file_count; i++) {
        if (strcmp(filesystem[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1; // File not found
}

// Function to add a directory or file with correct path
void add_to_filesystem(const char *name, int is_directory) {
    char full_path[MAX_PATH_LENGTH];
    if (name[0] == '/') {
        strncpy(full_path, name, MAX_PATH_LENGTH); // Абсолютный путь
    } else if (strcmp(current_path, "/") == 0) {
        snprintf(full_path, MAX_PATH_LENGTH, "/%s", name); // Корневой путь
    } else {
        snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", current_path, name); // Поддиректория
    }

    if (find_file(full_path) != -1) {
        printf("EXIST\n");
        return;
    }

    if (file_count >= MAX_FILES) {
        printf("Filesystem is full.\n");
        return;
    }

    FileEntry new_entry;
    strncpy(new_entry.filename, full_path, MAX_PATH_LENGTH);
    new_entry.size = 0;
    new_entry.start_cluster = FAT_FREE; // По умолчанию кластер не выделен
    new_entry.is_directory = is_directory;

    if (is_directory == 0 && new_entry.size > 0) {
        int cluster = allocate_cluster(&new_entry); // Попробовать выделить кластер
        if (cluster == -1) {
            printf("NO FREE CLUSTERS\n");
            return; // Если кластеры закончились, файл не добавляется
        }
        new_entry.start_cluster = cluster;
    }

    filesystem[file_count++] = new_entry;
    printf("OK\n");
}

// Function to list files in a directory
void ls(const char *dirname) {
    char target_path[MAX_PATH_LENGTH];
    if (dirname == NULL || strcmp(dirname, "") == 0) {
        strncpy(target_path, current_path, MAX_PATH_LENGTH);
    } else if (dirname[0] == '/') {
        strncpy(target_path, dirname, MAX_PATH_LENGTH);
    } else {
        snprintf(target_path, MAX_PATH_LENGTH, "%s/%s", current_path, dirname);
    }

    // Убедимся, что target_path оканчивается на "/"
    size_t target_len = strlen(target_path);
    if (target_path[target_len - 1] != '/') {
        strncat(target_path, "/", MAX_PATH_LENGTH - target_len - 1);
        target_len++;
    }

    int found = 0;
    for (size_t i = 0; i < file_count; i++) {
        // Сравниваем путь в файловой системе с target_path
        if (strncmp(filesystem[i].filename, target_path, target_len) == 0) {
            printf("%s: %s\n", filesystem[i].is_directory ? "DIR" : "FILE",
                   filesystem[i].filename + target_len);
            found = 1;
        }
    }

    if (!found) {
        printf("PATH NOT FOUND\n");
    }
}

// Function to change current directory
void cd(const char *dirname) {
    char new_path[MAX_PATH_LENGTH];
    if (strcmp(dirname, ".") == 0) {
        printf("OK\n");
        return;
    }

    if (strcmp(dirname, "..") == 0) {
        // Move up one directory
        char *last_slash = strrchr(current_path, '/');
        if (last_slash != NULL && last_slash != current_path) {
            *last_slash = '\0';
        } else {
            strcpy(current_path, "/");
        }
        printf("OK\n");
        return;
    }

    if (dirname[0] == '/') {
        strncpy(new_path, dirname, MAX_PATH_LENGTH);
    } else {
        snprintf(new_path, MAX_PATH_LENGTH, "%s/%s", current_path, dirname);
    }

    int dir_index = find_file(new_path);
    if (dir_index == -1 || !filesystem[dir_index].is_directory) {
        printf("PATH NOT FOUND\n");
        return;
    }

    strncpy(current_path, new_path, MAX_PATH_LENGTH);
    printf("OK - your current path is: %s\n",new_path);
}

// Function to print current working directory
void pwd() {
    printf("%s\n", current_path);
}

void create_directory(const char *dirname) {
    add_to_filesystem(dirname, 1);
}

int remove_directory(const char *dirname) {
    char full_path[MAX_PATH_LENGTH];
    if (dirname[0] == '/') {
        strncpy(full_path, dirname, MAX_PATH_LENGTH);
    } else {
        snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", current_path, dirname);
    }

    int dir_index = find_file(full_path);
    if (dir_index == -1 || !filesystem[dir_index].is_directory) {
        printf("FILE NOT FOUND\n");
        return -1;
    }

    for (size_t i = 0; i < file_count; i++) {
        if (strncmp(filesystem[i].filename, full_path, strlen(full_path)) == 0 &&
            strlen(filesystem[i].filename) > strlen(full_path)) {
            printf("NOT EMPTY\n");
            return -1;
            }
    }

    for (size_t i = dir_index; i < file_count - 1; i++) {
        filesystem[i] = filesystem[i + 1];
    }
    file_count--;

    printf("OK\n");
    return 0;
}

// Function to copy a file in the pseudo filesystem
void cp(const char *source, const char *destination) {
    int src_index = find_file(source);
    if (src_index == -1) {
        printf("FILE NOT FOUND\n");
        return;
    }

    if (find_file(destination) != -1) {
        printf("PATH NOT FOUND\n"); // File already exists
        return;
    }

    if (file_count >= MAX_FILES) {
        printf("Filesystem is full.\n");
        return;
    }

    FileEntry new_file;
    strncpy(new_file.filename, destination, MAX_PATH_LENGTH);
    new_file.size = filesystem[src_index].size;
    new_file.start_cluster = filesystem[src_index].start_cluster;
    new_file.is_directory = filesystem[src_index].is_directory;

    filesystem[file_count++] = new_file;

    printf("OK\n");
}

// Function to move or rename a file in the pseudo filesystem
void mv(const char *source, const char *destination) {
    int src_index = find_file(source);
    if (src_index == -1) {
        printf("FILE NOT FOUND\n");
        return;
    }

    if (find_file(destination) != -1) {
        printf("PATH NOT FOUND\n");
        return;
    }

    strncpy(filesystem[src_index].filename, destination, MAX_PATH_LENGTH);
    printf("OK\n");
}

// Function to remove a file in the pseudo filesystem
void rm(const char *filename) {
    int index = find_file(filename);
    if (index == -1) {
        printf("FILE NOT FOUND\n");
        return;
    }

    for (size_t i = index; i < file_count - 1; i++) {
        filesystem[i] = filesystem[i + 1];
    }
    file_count--;

    printf("OK\n");
}

// Function to display file content
void cat(const char *filename) {
    int file_index = find_file(filename);
    if (file_index == -1 || filesystem[file_index].is_directory) {
        printf("FILE NOT FOUND\n");
        return;
    }

    printf("OBSAH\n"); // Simulate file content output
}

void info(const char *name) {
    char full_path[MAX_PATH_LENGTH];
    if (name[0] == '/') {
        strncpy(full_path, name, MAX_PATH_LENGTH);
    } else {
        snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", current_path, name);
    }

    int index = find_file(full_path);
    if (index == -1) {
        printf("FILE NOT FOUND\n");
        return;
    }

    size_t start = filesystem[index].start_cluster;

    if (filesystem[index].is_directory) {
        printf("%s: Is a directory, no clusters allocated\n", filesystem[index].filename);
        return;
    }

    if (fat[start] == FAT_FREE) {
        printf("%s: No clusters allocated\n", filesystem[index].filename);
        return;
    }

    printf("%s: Clusters %zu", filesystem[index].filename, start);

    size_t current = start;
    while (fat[current] != FAT_END) {
        current = fat[current];
        printf(" -> %zu", current); // Добавляем вывод промежуточных кластеров
    }

    printf("\n");
}

void incp(const char *src_path, const char *dest_name) {
    FILE *src = fopen(src_path, "rb");
    if (!src) {
        printf("FILE NOT FOUND\n");
        return;
    }

    char full_path[MAX_PATH_LENGTH];
    snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", current_path, dest_name);

    if (find_file(full_path) != -1) {
        printf("PATH NOT FOUND\n");
        fclose(src);
        return;
    }

    if (file_count >= MAX_FILES) {
        printf("Filesystem is full.\n");
        fclose(src);
        return;
    }

    fseek(src, 0, SEEK_END);
    size_t file_size = ftell(src);
    rewind(src);

    FileEntry new_file;
    strncpy(new_file.filename, full_path, MAX_PATH_LENGTH);
    new_file.size = file_size;
    new_file.start_cluster = file_count; // Simplified clustering logic
    new_file.is_directory = 0;

    filesystem[file_count++] = new_file;
    fclose(src);
    printf("OK\n");
}

void outcp(const char *src_name, const char *dest_path) {
    char full_path[MAX_PATH_LENGTH];
    snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", current_path, src_name);

    int index = find_file(full_path);
    if (index == -1) {
        printf("FILE NOT FOUND\n");
        return;
    }

    FILE *dest = fopen(dest_path, "wb");
    if (!dest) {
        printf("PATH NOT FOUND\n");
        return;
    }

    // Simulated write
    fwrite("SIMULATED FILE DATA", 1, filesystem[index].size, dest);
    fclose(dest);
    printf("OK\n");
}

int execute_command(const char *command) {
    for (int i = 0; i < sizeof(command_table) / sizeof(command_table[0]); i++) {
        if (strncmp(command_table[i].command_name, command, strlen(command_table[i].command_name)) == 0) {
            command_table[i].command_func(command);
            return 0;
        }
    }
    printf("UNKNOWN COMMAND: %s\n", command);
    return -1;
}

int execute_command_with_args(const char *command) {
    // Найти первый пробел, который разделяет имя команды и её аргументы
    const char *space = strchr(command, ' ');
    char cmd_name[128] = {0};
    const char *args = NULL;

    if (space) {
        // Копируем имя команды
        strncpy(cmd_name, command, space - command);
        cmd_name[space - command] = '\0';
        args = space + 1; // Аргументы начинаются сразу после пробела
    } else {
        // Если пробела нет, вся строка — это имя команды
        strncpy(cmd_name, command, sizeof(cmd_name) - 1);
    }

    // Ищем команду в таблице
    for (int i = 0; i < sizeof(command_table) / sizeof(command_table[0]); i++) {
        if (strcmp(command_table[i].command_name, cmd_name) == 0) {
            // Вызываем функцию с аргументами (или NULL, если аргументов нет)
            command_table[i].command_func(args);
            return 0;
        }
    }

    // Если команда не найдена
    printf("UNKNOWN COMMAND: %s\n", cmd_name);
    return -1;
}

void load(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("FILE NOT FOUND\n");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0'; // Удаляем символ новой строки

        // Разделяем команду и аргументы
        char cmd_name[128] = {0};
        char args[128] = {0};

        // Используем sscanf для разделения строки на имя команды и аргументы
        int parts = sscanf(line, "%127s %127[^\n]", cmd_name, args);

        if (parts >= 1) { // Есть хотя бы команда
            printf("Executing: %s %s\n", cmd_name, parts == 2 ? args : "");
            if (parts == 2) {
                char command[100];
                snprintf(command, sizeof(command), "%s %s", cmd_name, args);
                execute_command_with_args(command);

            } else {
                execute_command(cmd_name);
            }
        } else {
            printf("UNKNOWN COMMAND FORMAT: %s\n", line);
        }
    }

    fclose(file);
    printf("OK\n");
}

void format(size_t size_mb, const char *filename) {
    // Размер файла в байтах
    size_t required_size = size_mb * 1024 * 1024;

    // Удаление содержимого файла (если файл существует)
    FILE *fs_file = fopen(filename, "wb");
    if (!fs_file) {
        printf("CANNOT CREATE FILE\n");
        return;
    }

    // Установка нового размера файла
    if (ftruncate(fileno(fs_file), required_size) != 0) {
        printf("CANNOT CREATE FILE\n");
        fclose(fs_file);
        return;
    }

    fclose(fs_file);
    printf("OK\n");
}

void test_functions() {
    initialize_filesystem();

    // Add files and directories
    add_to_filesystem("s1.txt", 0);
    filesystem[find_file("s1.txt")].size = 1024;
    add_to_filesystem("example.txt", 0);
    add_to_filesystem("a1", 1);

    // check();
    // bug();
}

void normalize_filesystem_paths() {
    for (size_t i = 0; i < file_count; i++) {
        char *src = filesystem[i].filename;
        char *dest = filesystem[i].filename;

        // Удаляем лишние слэши
        while (*src) {
            *dest = *src++;
            if (*dest != '/' || (dest == filesystem[i].filename || *(dest - 1) != '/')) {
                dest++;
            }
        }
        *dest = '\0';
    }
}

void bug(const char *arg) {
    // Для примера ожидаем, что аргумент содержит имя файла, который хотим повредить.
    // Например, "s1" означает файл "s1.txt" (или "/s1.txt" в файловой системе)
    char filename[MAX_PATH_LENGTH];
    if (arg == NULL || strcmp(arg, "") == 0) {
        printf("Usage: bug <filename_without_extension>\n");
        return;
    }
    // Формируем имя файла: можно, например, добавить расширение .txt
    snprintf(filename, MAX_PATH_LENGTH, "/%s.txt", arg);

    int index = find_file(filename);
    if (index == -1) {
        printf("FILE %s NOT FOUND\n", filename);
        return;
    }

    FileEntry *entry = &filesystem[index];
    if (entry->start_cluster == FAT_FREE) {
        printf("FILE %s has no clusters allocated.\n", filename);
        return;
    }

    // Попробуем повредить один из кластеров файла.
    // Если файл занимает хотя бы два кластера, повредим второй; иначе — первый.
    int current = entry->start_cluster;
    if (fat[current] == FAT_END) {
        // Файл занимает один кластер — повредим его
        fat[current] = -5; // устанавливаем "повреждённое" значение
        printf("Damaged cluster %d of file %s\n", current, filename);
        return;
    }
    // Файл занимает более одного кластера: переходим ко второму кластеру
    current = fat[current];
    fat[current] = -5;
    printf("Damaged cluster %d of file %s\n", current, filename);
}

void check() {
    int corrupted_found = 0;
    for (int i = 0; i < MAX_CLUSTERS; i++) {
        // Разрешённые значения: FAT_FREE, FAT_END или валидный указатель на следующий кластер
        if (fat[i] != FAT_FREE && fat[i] != FAT_END && (fat[i] < 0 || fat[i] >= MAX_CLUSTERS)) {
            printf("Cluster %d is corrupted: value %d\n", i, fat[i]);
            corrupted_found++;
        }
    }
    if (corrupted_found == 0)
        printf("Filesystem is OK\n");
    else
        printf("Total corrupted clusters: %d\n", corrupted_found);
}

void testBase() {
    initialize_filesystem();

    // Add files and directories
    add_to_filesystem("s1.txt", 0);
    filesystem[find_file("/s1.txt")].size = 1024;
    add_to_filesystem("example.txt", 0);
    add_to_filesystem("a1", 1);


    printf("Start Test...\n");
    // printf("1 - 4\n");
    // // 1 - 4
    // cp("//s1.txt","//a1/");
    // mv("//s1.txt","//a1/s1.txt");
    // // rm("//a1/s1.txt");
    // create_directory("a2");
    //
    // // 5 - 9
    // printf("5 - 9\n");
    // remove_directory("a2");
    //
    // printf("---------------\n");
    // ls("/");
    // printf("---------------\n");
    //
    // cat("//a1/s1.txt");
    // cd("//a1");
    // pwd();

    // rabochka ===========================

    //10 - 14
    for (size_t i = 0; i < file_count; i++) {
        printf("FILE: %s, IS_DIR: %d\n", filesystem[i].filename, filesystem[i].is_directory);
    }

    add_to_filesystem("/a1/file1.txt", 0);
    add_to_filesystem("/a1/file2.txt", 0);


    add_to_filesystem("/large_file.txt", 0);
    // int index = find_file("/large_file.txt");
    // filesystem[index].size = 7 * 4096; // Файл занимает 10 кластеров
    // allocate_cluster(&filesystem[index]);
    // info("/large_file.txt");

    int index = find_file("/large_file.txt");
    if (index != -1) {
        filesystem[index].size = 5 * 4096; // 7 кластеров по 4096 байт
        allocate_cluster(&filesystem[index]);
        info("/large_file.txt");

    } else {
        printf("Error: File not found in filesystem\n");
    }





    // info("/s1.txt"); true
    // incp("z.txt", "zzooss.txt"); //fix
    // ls(NULL);//fix
    // outcp("zzooss.txt", "zxc.txt"); //true
        // load("C:/v/commands.txt"); true
    format(10, "large_file.txt");
    printf("%llu\n",filesystem[index].size);
    // info("/large_file.txt");


    // ls(NULL);
    // printf("123\n");
    // cd("//");
    // ls(NULL);
}

int main(int argc, char *argv[]) {
    // if (argc != 2) {
    //     printf("Usage: %s <filesystem_file>\n", argv[0]);
    //     return EXIT_FAILURE;
    // }

    // Normally, you would load or initialize your pseudo filesystem here.
    // For testing purposes, we directly test the functions.
    // test_functions();
    testBase();
    // normalize_filesystem_paths();
    return EXIT_SUCCESS;
}





