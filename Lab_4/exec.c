#include "exec.h"
#include <stdio.h>
#include <stdlib.h>
#include "elf.h"
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>

#define STACK_SIZE 8192
static int PAGESIZE = 4096;

struct elf_header read_elf_header(const char *elfFile){
    // Open ELF file
    FILE *file = fopen(elfFile, "rb");
    printf("Opening binary %s\n", elfFile);             // Requirement of assignment 
    
    // Exit if file NULL
    if (file == NULL){
        printf("Error reading file!\n");
        exit(1);
    }

    struct elf_header header;
    // Read ELF file
    fread(&header, sizeof(struct elf_header), 1, file); 

    // Sanity Checks
    if (header.magic != ELF_MAGIC){
        printf("Error, this is not an ELF file!\n");
        exit(1);
    }
    if (header.machine != ELF_MACHINE_x64){
        printf("Error, wrong machine version!\n");
        exit(1);
    }
    if (header.version != ELF_VERSION){
        printf("Error, using wrong version!\n");
        exit(1);
    }
    
    // Close ELF file for read
    fclose(file);
    return header;
}

struct elf_proghdr* read_prog_headers(const char* elfFile, struct elf_header header) {
    int count = header.proghdr_count;
    FILE *file = fopen(elfFile, "rb");
    
    // List of program headers
    struct elf_proghdr *progHeaders = malloc(count * sizeof(struct elf_proghdr));

    // Move to the first Program Header file location
    fseek(file, header.proghdr_offset, SEEK_SET);

    // Read all Program Headers into the array
    fread(progHeaders,sizeof(struct elf_proghdr) * header.proghdr_count, 1,file);

    fclose(file);
    return progHeaders;
}

void dump_stack(void *stack) {
    uint64_t *stack_ptr = stack;
    int i = 0;

    fprintf(stderr, "Stack top: %p\n", stack_ptr);

    while (i++ < STACK_SIZE) {
        // print the 64-bit integer in hex with a space every 2 bytes
        fprintf(stderr, "[%4d] %p: %016lx   | [", i, stack_ptr, *stack_ptr);
        
        // Treat the 64-bit integer as a string and print it
        char *str = (char*) (stack_ptr );
        for (int j = 0; j < 8; j++) {
            if (str[j] == '\0') {
                fprintf(stderr, "\\0");
            } else {
                fprintf(stderr, "%c", str[j]);
            }
        }
        fprintf(stderr, "]\n");

        stack_ptr++;
    }
}

void dump_stack_light(void *stack) {
    uint64_t *stack_ptr = stack;
    printf("Stack top: %p\n", stack_ptr);
    printf("Stack bottom: %p\n", ((char*)(stack_ptr + STACK_SIZE))-1);
}

int main(int argc, char *argv[], char *envp[]) {  
    struct elf_header header = read_elf_header("helloworld_static");
    struct elf_proghdr *progHeaders = read_prog_headers("helloworld_static", header);
    int loadCount = 0;
    for (int i = 0; i < header.proghdr_count; i++) {
        if (progHeaders[i].type == ELF_PROG_LOAD){
            loadCount++;
        }
    }

    uint64_t totalSpan = 0;
    // iterate through array of program headers
    for (int i = 0; i < header.proghdr_count; i++) {
        if (progHeaders[i].type == ELF_PROG_LOAD) {
            // Part 2a
            // Determine the total span of all loadable segments
            // This should be correct according to Tudor
            loadCount--;
            if (loadCount == 0) {
                totalSpan = ROUNDUP(progHeaders[i].size_memory + progHeaders[i].virtual_address, PAGESIZE);
            }
        }
    }

    // Part 2a
    // Allocating memory for program headers using mmap()
    void *base = mmap(NULL, totalSpan, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Map file into memory
    int fd = open("helloworld_static", O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    void *fileBase = mmap(NULL, 10000000, PROT_READ, MAP_PRIVATE, fd, 0);
    if (fileBase == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Part 2b
    // Fill allocated memory with binary contents, taking into account all offsets to preserve structure
    for (int i = 0; i < header.proghdr_count; i++) {
        if (progHeaders[i].type == ELF_PROG_LOAD) {
            // Check if this memcpy works
            memcpy(base + progHeaders[i].virtual_address, fileBase + header.proghdr_offset + progHeaders[i].file_offset, progHeaders[i].size_memory);
            //  strcmp(fileBase + header.proghdr_offset + progHeaders[i].file_offset, base + progHeaders[i].virtual_address) ? printf("Error, memcpy failed!\n") : printf("Success!\n");
        }
    }

    
    // Part 2c
    // We're assuming this works
    // Set protection flags on memory regions based on program header flags
    for (int i = 0; i < header.proghdr_count; i++) {
        if (progHeaders[i].type == ELF_PROG_LOAD) {
            int prot = 0;
            if (progHeaders[i].flags & ELF_PROG_FLAG_READ) {
                prot |= PROT_READ;
            }
            if (progHeaders[i].flags & ELF_PROG_FLAG_WRITE) {
                prot |= PROT_WRITE;
            }
            if (progHeaders[i].flags & ELF_PROG_FLAG_EXEC) {
                prot |= PROT_EXEC;
            }
            // printf("PROT = %d\n", prot);
            // printf("FLAG = %d\n", progHeaders[i].flags);
            mprotect(base + progHeaders[i].virtual_address, progHeaders[i].size_memory, prot);
        }
    }

    // Part 4
    // Construct a fresh stack for the new program with the exact structure as described in appendix C
    uint64_t *stack = alloca(STACK_SIZE * sizeof(uint64_t));
    // printf("size of uint64: %ld\n", sizeof(uint64_t));
    memset(stack, '\0', STACK_SIZE * sizeof(uint64_t));
    
    // load argc into stack
    // -1 because we don't want to include exec
    stack[0] = (uint64_t) argc - 1;

    // load pointer to the first argv, then load the rest
    stack[1] = (uint64_t) &argv[1];
    for (int i = 2; i <= argc - 2; i++) {
        stack[i] = (uint64_t) &argv[i];
    }

    // NULL terminate argv in stack
    stack[argc - 1] = (uint64_t) NULL;

    // find envc
    int envc = 0;
    for (int i = 0; envp[i] != NULL; i++) {
        envc++;
    }

    // load pointer to the first envp, then load the rest
    stack[argc] = (uint64_t) &envp[0];
    for (int i = 0; i < envc; i++) {
        stack[argc + 1 + i] = (uint64_t) &envp[1 + i];
    }
    
    // NULL terminate envp in stack
    stack[argc + envc] = (uint64_t) NULL;

    // load pointer to the first auxv, then load the rest
    int auxvc = 0;
    stack[argc + envc + 1] = (uint64_t) envp[envc+2];
    for (int i = envc + 3; envp[i] != AT_NULL; i++) {
        stack[argc + envc + 1 + i] = (uint64_t) envp[i];
        auxvc++;
    }

    // NULL terminate auxv in stack
    stack[argc + envc + auxvc] = (uint64_t) NULL;

    dump_stack(stack);

    // Part 5: Set the exit point, load the stack pointer and jump to the entry point
    //jump(base + header.proghdr_offset, stack);
    
    // Freeing allocated mmap memory
    if (munmap(fileBase, 10000000) == -1) {
        perror("munmap");
        exit(1);
    }
    if (munmap(base, totalSpan) == -1) {
        perror("munmap");
        exit(1);
    }

    free(progHeaders);
    return 0;
}

// Part 4: Creating a stack

// Load the binary file into memory using fread(). It will return a pointer to the beginning of the binary
// in memory.

// Allocate memory for the binary file using mmap()

// Set proper permissions using of the chunk of memory using mprotect(). " This function accepts a bitwise-or 
// of flags for read, write, and execute"

// "The ‘virtual address’ is where the ELF binary expects us to load the segment into memory"