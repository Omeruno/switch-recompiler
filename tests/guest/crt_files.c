/* crt_files.c — файловый ввод-вывод через msvcrt и KERNEL32:
 * fopen/fprintf/fgets/fread/fseek в текстовом и двоичном режиме, каталоги,
 * поиск файлов, CreateFile/ReadFile, _stat, rename/remove, время. */
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <windows.h>

static int failures;
#define CHECK(cond) do { if (cond) printf("ok   %s\n", #cond); else { printf("FAIL %s (line %d)\n", #cond, __LINE__); failures++; } } while (0)

int main(void) {
    _mkdir("testdir");
    FILE* f = fopen("testdir\\Text.TXT", "w");
    CHECK(f != NULL);
    fprintf(f, "line one\nline %d\n", 2);
    fputs("third\n", f);
    fclose(f);

    /* текстовый режим: на диске \r\n */
    f = fopen("TESTDIR/text.txt", "rb");
    char raw[64] = {0};
    size_t n = fread(raw, 1, sizeof(raw), f);
    fclose(f);
    CHECK(n == 25 && raw[8] == '\r' && raw[9] == '\n');

    f = fopen("testdir\\text.txt", "r");
    char line[64];
    CHECK(fgets(line, sizeof(line), f) && strcmp(line, "line one\n") == 0);
    CHECK(fgets(line, sizeof(line), f) && strcmp(line, "line 2\n") == 0);
    int c = fgetc(f);
    ungetc(c, f);
    CHECK(fgets(line, sizeof(line), f) && strcmp(line, "third\n") == 0);
    CHECK(fgets(line, sizeof(line), f) == NULL && feof(f));
    fclose(f);

    /* двоичный режим, fseek/ftell */
    f = fopen("testdir\\data.bin", "w+b");
    int vals[100];
    for (int i = 0; i < 100; i++) vals[i] = i * i;
    CHECK(fwrite(vals, sizeof(int), 100, f) == 100);
    CHECK(ftell(f) == 400);
    fseek(f, 40, SEEK_SET);
    int v = 0;
    CHECK(fread(&v, sizeof(int), 1, f) == 1 && v == 100);
    fseek(f, -4, SEEK_END);
    CHECK(fread(&v, sizeof(int), 1, f) == 1 && v == 99 * 99);
    fclose(f);

    struct _stat st;
    CHECK(_stat("testdir\\data.bin", &st) == 0 && st.st_size == 400 && (st.st_mode & _S_IFREG));
    CHECK(_access("testdir\\nope.bin", 0) == -1);

    /* KERNEL32 */
    HANDLE h = CreateFileA("testdir\\data.bin", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(h != INVALID_HANDLE_VALUE);
    CHECK(GetFileSize(h, NULL) == 400);
    DWORD got = 0;
    SetFilePointer(h, 8, NULL, FILE_BEGIN);
    CHECK(ReadFile(h, &v, 4, &got, NULL) && got == 4 && v == 4);
    CloseHandle(h);
    CHECK(GetFileAttributesA("testdir") & FILE_ATTRIBUTE_DIRECTORY);
    CHECK(CreateFileA("testdir\\missing", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL) == INVALID_HANDLE_VALUE &&
          GetLastError() == ERROR_FILE_NOT_FOUND);

    /* поиск файлов */
    WIN32_FIND_DATAA fd;
    HANDLE fh = FindFirstFileA("testdir\\*.txt", &fd);
    CHECK(fh != INVALID_HANDLE_VALUE && _stricmp(fd.cFileName, "text.txt") == 0);
    CHECK(!FindNextFileA(fh, &fd));
    FindClose(fh);
    struct _finddata_t fdt;
    intptr_t fr = _findfirst("testdir\\*.bin", &fdt);
    CHECK(fr != -1 && fdt.size == 400);
    _findclose(fr);

    /* rename/remove, текущий каталог */
    CHECK(rename("testdir\\data.bin", "testdir\\moved.bin") == 0);
    CHECK(remove("testdir\\moved.bin") == 0 && _access("testdir\\moved.bin", 0) == -1);
    char cwd[MAX_PATH];
    CHECK(_getcwd(cwd, sizeof(cwd)) && cwd[1] == ':');
    CHECK(_chdir("testdir") == 0 && fopen("text.txt", "r") != NULL);
    _chdir("..");
    remove("testdir\\text.txt");
    CHECK(_rmdir("testdir") == 0);

    time_t t = time(NULL);
    CHECK(t > 1700000000);
    struct tm* tmv = gmtime(&t);
    CHECK(tmv && tmv->tm_year >= 123);
    char tb[64];
    CHECK(strftime(tb, sizeof(tb), "%Y-%m-%d", tmv) == 10);

    printf("crt_files: %s\n", failures ? "FAILED" : "PASSED");
    return failures;
}
