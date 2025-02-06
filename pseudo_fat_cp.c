#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>


#define MAX_PATH_LENGTH 256
#define CLUSTER_SIZE 4096
#define MAX_CLUSTERS 4096
#define FAT_FREE (-1)
#define FAT_END (-2)

int fat[MAX_CLUSTERS];
static size_t cluster_count = 0;

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

void cp(const char *arg1);
void mv(const char *arg1);
void rm(const char *arg1);
void create_directory(const char *arg);
int remove_directory(const char *arg);
void ls(const char *arg);
void cat(const char *arg);
void cd(const char *arg);
void pwd();
void info(const char *arg);
void incp(const char *arg1);
void outcp(const char *arg1);
void format(const char *arg);
void load(const char *filename);
void normalize_path();
void bug(const char *arg);
void check();
void fs_info();
int count_free_clusters();

void remove_directory_wrapper(const char *arg) {
    remove_directory(arg); // Вызов оригинальной функции с адаптированным аргументом
}

Command command_table[] = {
    {"cp", (void (*)(const char *))cp},
    {"mv", (void (*)(const char *))mv},
    {"rm", rm},
    {"mkdir", create_directory},
    {"rmdir", remove_directory_wrapper}, // Используем обёртку
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
char disk_filename[MAX_PATH_LENGTH];  // Здесь сохраним имя файла, переданного при запуске


void fs_info() {
    // 1) Узнаём размер файла-образа через stat()
    struct stat st;
    if (stat(disk_filename, &st) != 0) {
        // Если ошибка, выводим сообщение
        printf("Cannot determine filesystem size (stat error: %d)\n", errno);
        return;
    }

    // st.st_size — реальный размер файла-образа в байтах
    printf("Filesystem total size: %zu bytes (%zu MB)\n", (size_t)st.st_size,(size_t)st.st_size / 1024/1024);


    cluster_count = (size_t)st.st_size / CLUSTER_SIZE;

    // 2) Подсчитаем свободные и занятые кластеры
    int free_clusters = 0;
    int used_clusters = 0;
    for (int i = 0; i < cluster_count; i++) {
        if (fat[i] == FAT_FREE) {
            free_clusters++;
        } else {
            used_clusters++;
        }
    }
    printf("Total clusters: %llu\n", cluster_count);
    printf("Used clusters: %d\n", used_clusters);
    printf("Free clusters: %d\n", free_clusters);

    // Если хотите в байтах:
    printf("Approx. used space: %zu bytes (%zu MB)\n", (size_t)used_clusters * CLUSTER_SIZE,
        (size_t)used_clusters * CLUSTER_SIZE/1024/1024);
    printf("Approx. free space: %zu bytes (%zu MB)\n", (size_t)free_clusters * CLUSTER_SIZE,
        (size_t)free_clusters * CLUSTER_SIZE/1024/1024);
}

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
    if (count_free_clusters() < clusters_needed) {
        return -1;  // Недостаточно места
    }

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
    normalize_path(full_path, name);

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
    new_entry.start_cluster = FAT_FREE;
    new_entry.is_directory = is_directory;

    // 🔥 Новый фикс: если это папка, убеждаемся, что путь заканчивается на `/`
    if (is_directory && full_path[strlen(full_path) - 1] != '/') {
        strncat(new_entry.filename, "/", MAX_PATH_LENGTH - strlen(new_entry.filename) - 1);
    }

    filesystem[file_count++] = new_entry;
    printf("OK\n");
}


// Function to list files in a directory
void ls(const char *dirname) {
    char target_path[MAX_PATH_LENGTH];
    // printf("zv-%s-zv\n", dirname);
    if (dirname == NULL || strcmp(dirname, "") == 0 || strcmp(dirname, "ls") == 0) {
        strncpy(target_path, current_path, MAX_PATH_LENGTH);
    } else {
        normalize_path(target_path, dirname);
    }

    size_t target_len = strlen(target_path);
    if (target_path[target_len - 1] != '/') {
        strncat(target_path, "/", MAX_PATH_LENGTH - target_len - 1);
        target_len++;
    }

    int found = 0;
    int dir_exists = 0;

    // Проверяем, существует ли папка
    for (size_t i = 0; i < file_count; i++) {
        if (strcmp(filesystem[i].filename, target_path) == 0 && filesystem[i].is_directory) {
            dir_exists = 1;
            break;
        }
    }

    // Проверяем, есть ли файлы или подпапки внутри каталога
    for (size_t i = 0; i < file_count; i++) {
        if (strncmp(filesystem[i].filename, target_path, target_len) == 0) {
            dir_exists = 1;
            break;
        }
    }

    if (!dir_exists) {
        printf("PATH NOT FOUND\n");
        return;
    }

    // Проверяем содержимое директории
    for (size_t i = 0; i < file_count; i++) {
        if (strncmp(filesystem[i].filename, target_path, target_len) == 0) {
            const char *subpath = filesystem[i].filename + target_len;

            // Пропускаем вложенные файлы и папки (оставляем только элементы первого уровня)
            // if (strchr(subpath, '/') != NULL || strlen(subpath) == 0) {
            //     printf("zzz\n");
            //     continue;
            // }
            // 🔥 Новый фикс: Ищем первый `/` после target_path

            if (strlen(subpath) == 0) {
                continue;
            }

            // Проверяем, есть ли символы после `/` в `subpath`
            // char *slash_pos = strchr(subpath, '/');
            // if (slash_pos != NULL && *(slash_pos + 1) != '\0') {
            //     continue;  // Если после '/' есть символы, пропускаем этот элемент
            // }


            printf("%s: %s\n", filesystem[i].is_directory ? "DIR" : "FILE", subpath);
            found = 1;
        }
    }

    if (!found) {
        printf("EMPTY\n");
    }
}


// Function to change current directory
void cd(const char *dirname) {
    char new_path[MAX_PATH_LENGTH];
    normalize_path(new_path, dirname);

    if (strcmp(dirname, ".") == 0) {
        printf("OK\n");
        return;
    }

    if (strcmp(dirname, "..") == 0) {
        // Подняться на уровень выше
        char *last_slash = strrchr(current_path, '/');
        if (last_slash != NULL && last_slash != current_path) {
            *last_slash = '\0';
        } else {
            strcpy(current_path, "/");
        }
        printf("OK - current path: %s\n", current_path);
        return;
    }

    int dir_index = find_file(new_path);
    if (dir_index == -1 || !filesystem[dir_index].is_directory) {
        printf("PATH NOT FOUND\n");
        return;
    }

    strncpy(current_path, new_path, MAX_PATH_LENGTH);
    printf("OK - current path: %s\n", current_path);
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
    normalize_path(full_path, dirname);

    int dir_index = find_file(full_path);
    if (dir_index == -1 || !filesystem[dir_index].is_directory) {
        printf("FILE NOT FOUND\n");
        return -1;
    }

    // Проверяем, есть ли внутри директории файлы/папки
    for (size_t i = 0; i < file_count; i++) {
        if (strncmp(filesystem[i].filename, full_path, strlen(full_path)) == 0 &&
            strlen(filesystem[i].filename) > strlen(full_path)) {

            // Если внутри есть файлы или папки, рекурсивно удаляем их
            remove_directory(filesystem[i].filename);
            }
    }

    // Удаляем саму директорию
    for (size_t i = dir_index; i < file_count - 1; i++) {
        filesystem[i] = filesystem[i + 1];
    }
    file_count--;

    printf("OK\n");
    return 0;
}

// Function to copy a file in the pseudo filesystem
void cp(const char *args) {
    if (!args || strlen(args) == 0) {
        printf("INVALID ARGUMENTS\n");
        return;
    }

    char source[MAX_PATH_LENGTH], destination[MAX_PATH_LENGTH];

    // Разбираем строку args: "f1 a1"
    int parsed = sscanf(args, "%s %s", source, destination);
    if (parsed != 2) {
        printf("INVALID ARGUMENTS\n");
        return;
    }

    // Проверка: destination не должен быть пустым или "/"
    if (strlen(destination) == 0 || strcmp(destination, "/") == 0) {
        printf("INVALID DESTINATION NAME\n");
        return;
    }

    // Проверяем, заканчивается ли destination на "/"
    if (destination[strlen(destination) - 1] == '/') {
        printf("INVALID DESTINATION NAME: Cannot copy to a directory without a filename\n");
        return;
    }

    printf("source: %s _ dest: %s\n", source, destination); // Отладочный вывод

    char src_path[MAX_PATH_LENGTH], dest_path[MAX_PATH_LENGTH];
    normalize_path(src_path, source);
    normalize_path(dest_path, destination);

    int src_index = find_file(src_path);
    if (src_index == -1) {
        printf("FILE NOT FOUND\n");
        return;
    }

    FileEntry *src_entry = &filesystem[src_index];

    // Проверяем, существует ли уже объект с таким именем
    if (find_file(dest_path) != -1) {
        printf("PATH NOT FOUND (alrdy exists)\n"); // Файл уже существует
        return;
    }

    if (file_count >= MAX_FILES) {
        printf("Filesystem is full.\n");
        return;
    }

    // Если source — это папка, рекурсивно копируем её содержимое
    if (src_entry->is_directory) {
        printf("Copying directory %s -> %s\n", src_path, dest_path);

        // Создаём новую папку
        add_to_filesystem(dest_path, 1);

        // Копируем всё содержимое
        size_t src_len = strlen(src_path);
        for (size_t i = 0; i < file_count; i++) {
            if (strncmp(filesystem[i].filename, src_path, src_len) == 0 &&
                filesystem[i].filename[src_len] == '/') {

                char new_dest[MAX_PATH_LENGTH];
                snprintf(new_dest, MAX_PATH_LENGTH, "%s%s", dest_path, filesystem[i].filename + src_len);

                char sub_args[MAX_PATH_LENGTH * 2];
                snprintf(sub_args, sizeof(sub_args), "%s %s", filesystem[i].filename, new_dest);

                cp(sub_args);
            }
        }
    } else {
        // Копируем обычный файл
        FileEntry new_file;
        strncpy(new_file.filename, dest_path, MAX_PATH_LENGTH);
        new_file.size = src_entry->size;
        new_file.start_cluster = src_entry->start_cluster;
        new_file.is_directory = 0;

        filesystem[file_count++] = new_file;
    }

    printf("OK\n");
}

// Function to move or rename a file in the pseudo filesystem
void mv(const char *args) {
    if (!args || strlen(args) == 0) {
        printf("INVALID ARGUMENTS\n");
        return;
    }

    char source[MAX_PATH_LENGTH], destination[MAX_PATH_LENGTH];

    // Разбираем строку args: "f1 a1"
    int parsed = sscanf(args, "%s %s", source, destination);
    if (parsed != 2) {
        printf("INVALID ARGUMENTS\n");
        return;
    }

    printf("source: %s _ dest: %s\n", source, destination); // Отладочный вывод

    char src_path[MAX_PATH_LENGTH], dest_path[MAX_PATH_LENGTH];
    normalize_path(src_path, source);
    normalize_path(dest_path, destination);

    int src_index = find_file(src_path);
    if (src_index == -1) {
        printf("FILE NOT FOUND\n");
        return;
    }

    // Проверяем, существует ли уже объект с таким именем
    int dest_index = find_file(dest_path);

    if (dest_index != -1 && filesystem[dest_index].is_directory) {
        // Если destination — это папка, добавляем к пути имя файла
        char final_dest[MAX_PATH_LENGTH];
        snprintf(final_dest, MAX_PATH_LENGTH, "%s/%s", dest_path, strrchr(src_path, '/') ? strrchr(src_path, '/') + 1 : src_path);
        strncpy(dest_path, final_dest, MAX_PATH_LENGTH);
    }

    // Проверяем, существует ли уже файл/папка с таким именем
    if (find_file(dest_path) != -1) {
        printf("PATH NOT FOUND\n"); // Файл уже существует
        return;
    }

    // Переименовываем/перемещаем файл
    strncpy(filesystem[src_index].filename, dest_path, MAX_PATH_LENGTH);
    printf("OK\n");
}

// Function to remove a file in the pseudo filesystem
void rm(const char *filename) {
    char full_path[MAX_PATH_LENGTH];
    normalize_path(full_path, filename);

    int index = find_file(full_path);
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

void incp(const char *args) {
    if (!args || strlen(args) == 0) {
        printf("INVALID ARGUMENTS\n");
        return;
    }

    char source[MAX_PATH_LENGTH], destination[MAX_PATH_LENGTH];

    // Разбираем строку args: "source destination"
    int parsed = sscanf(args, "%s %s", source, destination);
    if (parsed != 2) {
        printf("INVALID ARGUMENTS\n");
        return;
    }

    FILE *src = fopen(source, "rb");
    if (!src) {
        printf("FILE NOT FOUND\n");
        return;
    }

    char full_path[MAX_PATH_LENGTH];
    normalize_path(full_path, destination);

    if (find_file(full_path) != -1) {
        printf("EXIST\n");
        fclose(src);
        return;
    }

    if (file_count >= MAX_FILES) {
        printf("Filesystem is full.\n");
        fclose(src);
        return;
    }

    // Определяем размер файла
    fseek(src, 0, SEEK_END);
    size_t file_size = ftell(src);
    rewind(src);

    // Создаём новый файл в файловой системе
    FileEntry new_file;
    strncpy(new_file.filename, full_path, MAX_PATH_LENGTH);
    new_file.size = file_size;
    new_file.start_cluster = allocate_cluster(&new_file);
    new_file.is_directory = 0;

    if (file_size/CLUSTER_SIZE < count_free_clusters()) {
        printf("NO FREE CLUSTERS\n");
        fclose(src);
        return;
    }

    // Записываем данные в псевдо-FS (симуляция)
    filesystem[file_count++] = new_file;

    fclose(src);
    printf("OK\n");
}

void outcp(const char *args) {
    if (!args || strlen(args) == 0) {
        printf("INVALID ARGUMENTS\n");
        return;
    }

    char source[MAX_PATH_LENGTH], destination[MAX_PATH_LENGTH];

    // Разбираем строку args: "source destination"
    int parsed = sscanf(args, "%s %s", source, destination);
    if (parsed != 2) {
        printf("INVALID ARGUMENTS\n");
        return;
    }

    char full_path[MAX_PATH_LENGTH];
    normalize_path(full_path, source);

    int index = find_file(full_path);
    if (index == -1) {
        printf("FILE NOT FOUND\n");
        return;
    }

    FILE *dest = fopen(destination, "wb");
    if (!dest) {
        printf("PATH NOT FOUND\n");
        return;
    }

    FileEntry *file = &filesystem[index];

    // 🔥 Симуляция чтения данных из псевдо-FAT (пока что просто заполняем нулями)
    char *buffer = (char *)calloc(1, file->size);
    fwrite(buffer, 1, file->size, dest);
    free(buffer);

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

void format(const char *arg) {
    // arg может быть "600MB" или просто "600" и т.п.

    if (!arg || !*arg) {
        printf("CANNOT CREATE FILE\n");
        return;
    }

    // Разбираем строку "600MB" на число (600) и суффикс (MB)
    long size_mb = 0;
    char suffix[8] = {0};

    // Пример простого разбора: берем число и до 2 символов суффикса
    // Если строка "600MB", то size_mb=600, suffix="MB"
    // Если "600", то size_mb=600, suffix=""
    if (sscanf(arg, "%ld%2s", &size_mb, suffix) < 1) {
        // Не смогли хотя бы число считать
        printf("CANNOT CREATE FILE\n");
        return;
    }

    // Проверка суффикса (если надо строго требовать "MB", делайте иначе)
    // Допустим, разрешим и без суффикса:
    if (strcasecmp(suffix, "MB") != 0 && suffix[0] != '\0') {
        // Если суффикс не "MB" и не пуст, считаем ошибкой
        printf("CANNOT CREATE FILE\n");
        return;
    }

    if (size_mb <= 0) {
        printf("CANNOT CREATE FILE\n");
        return;
    }

    // Переводим мегабайты в байты
    size_t required_size = (size_t)size_mb * 1024 * 1024;

    // Используем файл, заданный при запуске (disk_filename)
    FILE *fs_file = fopen(disk_filename, "wb");
    if (!fs_file) {
        printf("CANNOT CREATE FILE\n");
        return;
    }

    // Устанавливаем новый размер
    if (ftruncate(fileno(fs_file), required_size) != 0) {
        fclose(fs_file);
        printf("CANNOT CREATE FILE\n");
        return;
    }

    fclose(fs_file);

    // Сбрасываем/инициализируем вашу псевдо-ФС в памяти:
    initialize_filesystem();

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

void normalize_path(char *normalized_path, const char *input_path) {
    if (input_path[0] == '/') {
        // Уже абсолютный путь
        strncpy(normalized_path, input_path, MAX_PATH_LENGTH);
    } else {
        // Добавляем текущий путь
        if (strcmp(current_path, "/") == 0) {
            snprintf(normalized_path, MAX_PATH_LENGTH, "/%s", input_path);
        } else {
            snprintf(normalized_path, MAX_PATH_LENGTH, "%s/%s", current_path, input_path);
        }
    }

    // Убираем двойные слэши, если они появились
    char *src = normalized_path, *dst = normalized_path;
    while (*src) {
        *dst = *src++;
        if (*dst != '/' || (dst == normalized_path || *(dst - 1) != '/')) {
            dst++;
        }
    }
    *dst = '\0';
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

int count_free_clusters() {
    int free_clusters = 0;
    for (int i = 0; i < MAX_CLUSTERS; i++) {
        if (fat[i] == FAT_FREE) {
            free_clusters++;
        }
    }
    return free_clusters;
}


void testBase() {
    initialize_filesystem();

    // Add files and directories
    add_to_filesystem("s1.txt", 0);
    filesystem[find_file("/s1.txt")].size = 1024;
    add_to_filesystem("example.txt", 0);
    add_to_filesystem("a1", 1);

    printf("Start Test...\n");
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
    // format("10MB");

    add_to_filesystem("/large_file.txt", 0);
    // int index = find_file("/large_file.txt");
    // filesystem[index].size = 7 * 4096; // Файл занимает 10 кластеров
    // allocate_cluster(&filesystem[index]);
    // info("/large_file.txt");

    int index = find_file("/large_file.txt");
    if (index != -1) {
        filesystem[index].size = 50 * 4096; // 7 кластеров по 4096 байт
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


    printf("%llu\n",filesystem[index].size);
    info("/large_file.txt");
    fs_info();

    bug("large_file");
    check();


    // ls(NULL);
    // printf("123\n");
    // cd("//");
    // ls(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <filesystem_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    strncpy(disk_filename, argv[1], MAX_PATH_LENGTH);
    disk_filename[MAX_PATH_LENGTH - 1] = '\0'; // защита от переполнения

    initialize_filesystem();
    add_to_filesystem("f1", 0);
    add_to_filesystem("f2", 0);
    // format("10MB");
    add_to_filesystem("a1", 1);
    add_to_filesystem("a1/f3", 0);
    add_to_filesystem("aue", 1);


        // testBase();
    char line[256];
    while (1) {
        printf("myFS> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            // Если fgets вернул NULL, значит EOF или ошибка
            break;
        }

        // Убираем \n в конце
        line[strcspn(line, "\r\n")] = '\0';

        // Если пользователь ввёл пустую строку — пропускаем
        if (strlen(line) == 0) {
            continue;
        }

        // Можно сделать команду "exit" для выхода
        if (strcmp(line, "exit") == 0) {
            printf("Bye!\n");
            break;
        }

        // Передаём команду в ваш парсер
        execute_command_with_args(line);
    }

    return EXIT_SUCCESS;
}





