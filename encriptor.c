#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>

#define NUM_OF_PROC 12

void check_args(int arguments_count, char *argv[]) // valid call checker
{
    if (arguments_count != 2 && arguments_count != 3)
    {
        const char *format = 
            "Incorrect use!\n"
            "How to use:\n"
            " For encrypting: %s <file_to_encrypt>\n"
            " For decrypting: %s <file_to_decrypt> <permutation_file>\n";

        char err_msg[256];
        sprintf(err_msg, format, argv[0], argv[0]);
        write(STDERR_FILENO, err_msg, strlen(err_msg));
        exit(1);
    }
}

void can_open_file(int file_descriptor) // check file open
{
    if(file_descriptor == -1)
    {
        const char *err_msg = "File couldn't be opened.\n";
        write(STDERR_FILENO, err_msg, strlen(err_msg));
        exit(1);
    }
}

void check_file_size(int size)
{
    if(size == 0)
    {
        const char *err_msg = "The provided file is empty.\n";
        write(STDERR_FILENO, err_msg, strlen(err_msg));
        exit(1);
    }
}

void can_open_mem_obj(int shm_file_descriptor)
{
    if(shm_file_descriptor < 0)
    {
        perror(NULL);
        const char *err_msg = "Cannot open shared memory object.\n";
        write(STDERR_FILENO, err_msg, strlen(err_msg));
        exit(1);
    }
}

void shuffle(int *arr, int n)
{
    for(int i = 0; i < n; i++)
    {
        arr[i] = i;
    }
    for(int i = n - 1; i > 0; i--)
    {
        int rand_index = rand() % (i + 1);
        int tmp = arr[i];
        arr[i] = arr[rand_index];
        arr[rand_index] = tmp;
    }
}

int main(int argc, char *argv[]) // MAIN
{
    check_args(argc, argv);
    srand(time(NULL));
	
	//the file to encrypt/decrypt
    int fd_in = open(argv[1], O_RDONLY);
    can_open_file(fd_in);

    struct stat st_file_in;
    if(fstat(fd_in, &st_file_in) < 0)
    {
        const char *err_msg = "Cannot get information about the file.\n";
        write(STDERR_FILENO, err_msg, strlen(err_msg));
        exit(1);
    }
    //file cannot be empty
    check_file_size(st_file_in.st_size);

    size_t original_size = st_file_in.st_size;
    
    size_t total_size = 3 * original_size; 


	//memory object
    char shm_name[] = "shm_words";
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    can_open_mem_obj(shm_fd);

	//dimension for obj
    if(ftruncate(shm_fd, total_size) < 0)
    {
        perror("ftruncate");
        shm_unlink(shm_name);
        exit(1);
    }

	//load
    char *shm_ptr = mmap(0, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm_ptr == MAP_FAILED)
    {
        perror("mmap");
        shm_unlink(shm_name);
        exit(1);
    }

    ssize_t bytes_read_num = read(fd_in, shm_ptr, original_size);
    if(bytes_read_num < 0 || (size_t)bytes_read_num != original_size)
    {
        perror("read");
        shm_unlink(shm_name);
        exit(1);
    }
    close(fd_in);

    //  [0 .. original_size) = original word list
    //  [original_size ... 2*original_size) = encrypted word list
    //  [2*original_size ... 3*original_size) = permutations

    char *enc_area  = shm_ptr + original_size; 
    char *perm_area = shm_ptr + (2 * original_size); 

    const int MAX_WORDS = 100000;
    int *start_index_word = malloc(MAX_WORDS * sizeof(int));
    int *length_word = malloc(MAX_WORDS * sizeof(int));

    int words_counter = 0;
    int indx = 0;
    
    int start_of_word = -1;

    for(int i = 0; i < original_size; i++)
    {
        char ch = shm_ptr[i];
        if(ch == ' ' || ch == '\t' || ch == '\n')
        {
            if(start_of_word != -1) // has been set
            {
                start_index_word[words_counter] = start_of_word;
                length_word[words_counter] = i - start_of_word;
                ++words_counter;
                start_of_word = -1;
            }
        }
        else
        {
            if(start_of_word == -1)
            {
                start_of_word = i;
                indx = i;
            }
        }
    }
    //last word
    if(start_of_word != -1)
    {
        start_index_word[words_counter] = indx;
        length_word[words_counter] = original_size - start_of_word;
        words_counter++;
    }

    if(argc == 2) // encrypt file.txt
    {
        int fd_encrypted = open("encrypted.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        int fd_permutations = open("permutations.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

        can_open_file(fd_encrypted);
        can_open_file(fd_permutations);

        
        int *enc_offset = calloc(words_counter, sizeof(int));
        int total_enc = 0;
        for(int w=0; w<words_counter; w++)
        {
            enc_offset[w] = total_enc;
            total_enc += length_word[w] + 1; 
        }

        int *perm_offset = calloc(words_counter, sizeof(int));
        int total_perm = 0;
        for(int w=0; w<words_counter; w++)
        {
            perm_offset[w] = total_perm;
            total_perm += (length_word[w]*4 + 10);
        }

        int words_to_handle_per_proc = (words_counter + NUM_OF_PROC -1) / NUM_OF_PROC;

        for(int p = 0; p < NUM_OF_PROC; p++)
        {
            int proc_begin = p * words_to_handle_per_proc;
            int proc_end = (p+1) * words_to_handle_per_proc;
            if(proc_end > words_counter) 
                proc_end = words_counter;
            if(proc_begin >= proc_end) 
                break;

            pid_t pid = fork();
            if(pid < 0)
            {
                perror("fork");
                exit(1);
            }
            else if(pid == 0)
            {
                for(int w = proc_begin; w < proc_end; w++)
                {
                    int wstart = start_index_word[w];
                    int wlen = length_word[w];

                    char *tempbuf = malloc(wlen+1);
                    char *resbuf = malloc(wlen+1);
                    int *perm = malloc(wlen*sizeof(int));

                    memcpy(tempbuf, shm_ptr + wstart, wlen);
                    tempbuf[wlen] = '\0';


                    shuffle(perm, wlen);
                    for(int i=0; i<wlen; i++)
                    {
                        resbuf[i] = tempbuf[perm[i]];
                    }
                    resbuf[wlen] = '\0';

                    
                    char *dst_enc = enc_area + enc_offset[w];
                    memcpy(dst_enc, resbuf, wlen);
                    dst_enc[wlen] = '\n'; 

                    char *dst_perm = perm_area + perm_offset[w];
                    int pos = 0;
                    for(int i=0; i<wlen; i++)
                    {
                        pos += sprintf(dst_perm + pos, "%d", perm[i]);
                        if(i<wlen-1) 
                            dst_perm[pos++] = ' ';
                    }
                    dst_perm[pos++] = '\n';
                    dst_perm[pos]   = '\0';

                    free(tempbuf);
                    free(resbuf);
                    free(perm);
                }
                _exit(0);
            }
        }

        for(int p=0; p<NUM_OF_PROC; p++)
        {
            wait(NULL);
        }

        for(int w=0; w<words_counter; w++)
        {
            char *src_enc = enc_area + enc_offset[w];
            int  length_enc = length_word[w] + 1; 
            write(fd_encrypted, src_enc, length_enc);
        }

        for(int w=0; w<words_counter; w++)
        {
            char *src_perm = perm_area + perm_offset[w];
            write(fd_permutations, src_perm, strlen(src_perm));
        }

        close(fd_encrypted);
        close(fd_permutations);

        free(enc_offset);
        free(perm_offset);

        free(start_index_word);
        free(length_word);

        munmap(shm_ptr, total_size);
        shm_unlink(shm_name);
    }
    else if(argc == 3) // decryption
    {
        int fd_enc = open(argv[1], O_RDONLY);
        can_open_file(fd_enc);

        int fd_perm = open(argv[2], O_RDONLY);
        can_open_file(fd_perm);

        struct stat st_enc, st_perm;
        if(fstat(fd_enc, &st_enc) < 0)
        {
            const char *err_msg = "Cannot get information about the encrypted file.\n";
            write(STDERR_FILENO, err_msg, strlen(err_msg));
            exit(1);
        }
        if(fstat(fd_perm, &st_perm) < 0)
        {
            const char *err_msg = "Cannot get information about the permutations file.\n";
            write(STDERR_FILENO, err_msg, strlen(err_msg));
            exit(1);
        }

        size_t enc_size = st_enc.st_size;
        size_t perm_size = st_perm.st_size;
        size_t total_size = enc_size + perm_size + enc_size; // original_size = enc_size

        shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        can_open_mem_obj(shm_fd);

        if(ftruncate(shm_fd, total_size) < 0)
        {
            perror("ftruncate");
            shm_unlink(shm_name);
            exit(1);
        }

        shm_ptr = mmap(0, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if(shm_ptr == MAP_FAILED)
        {
            perror("mmap");
            shm_unlink(shm_name);
            exit(1);
        }

        ssize_t read_enc = read(fd_enc, shm_ptr, enc_size);
        if(read_enc < 0 || (size_t)read_enc != enc_size)
        {
            perror("read encrypted.txt");
            shm_unlink(shm_name);
            exit(1);
        }
        close(fd_enc);

        ssize_t read_perm = read(fd_perm, shm_ptr + enc_size, perm_size);
        if(read_perm < 0 || (size_t)read_perm != perm_size)
        {
            perror("read permutations.txt");
            shm_unlink(shm_name);
            exit(1);
        }
        close(fd_perm);

        //  [0 .. enc_size) = encrypted word list
        //  [enc_size ... enc_size + perm_size) = permutations
        //  [enc_size + perm_size ... enc_size + perm_size + enc_size) = decrypted words

        char *dec_area = shm_ptr + enc_size + perm_size;

        const int MAX_WORDS = 100000;
        int *start_index_word = malloc(MAX_WORDS * sizeof(int));
        int *length_word = malloc(MAX_WORDS * sizeof(int));

        int words_counter = 0;
        int start_of_word = -1;
        for(int i = 0; i < enc_size; i++)
        {
            char ch = shm_ptr[i];
            if(ch == ' ' || ch == '\t' || ch == '\n')
            {
                if(start_of_word != -1)
                {
                    start_index_word[words_counter] = start_of_word;
                    length_word[words_counter] = i - start_of_word;
                    words_counter++;
                    start_of_word = -1;
                }
            }
            else
            {
                if(start_of_word == -1)
                {
                    start_of_word = i;
                }
            }
        }
        
        if(start_of_word != -1)
        {
            start_index_word[words_counter] = start_of_word;
            length_word[words_counter] = enc_size - start_of_word;
            words_counter++;
        }

        
        int *perm_start = malloc(words_counter * sizeof(int));
        int *perm_length = malloc(words_counter * sizeof(int));

        int perm_words_counter = 0;
        int perm_start_word = -1;
        for(int i = 0; i < perm_size; i++)
        {
            char ch = shm_ptr[enc_size + i];
            if(ch == '\n')
            {
                if(perm_start_word != -1)
                {
                    perm_start[perm_words_counter] = perm_start_word;
                    perm_length[perm_words_counter] = i - perm_start_word;
                    perm_words_counter++;
                    perm_start_word = -1;
                }
            }
            else
            {
                if(perm_start_word == -1)
                {
                    perm_start_word = i;
                }
            }
        }
        
        if(perm_start_word != -1)
        {
            perm_start[perm_words_counter] = perm_start_word;
            perm_length[perm_words_counter] = perm_size - perm_start_word;
            perm_words_counter++;
        }

        
        if(words_counter != perm_words_counter)
        {
            const char *err_msg = "Mismatch between number of encrypted words and permutations.\n";
            write(STDERR_FILENO, err_msg, strlen(err_msg));
            shm_unlink(shm_name);
            exit(1);
        }

        int fd_decrypted = open("decrypted.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        can_open_file(fd_decrypted);

        int *dec_offset = calloc(words_counter, sizeof(int));
        int total_dec = 0;
        for(int w=0; w<words_counter; w++)
        {
            dec_offset[w] = total_dec;
            total_dec += (length_word[w] + 1);
        }

        int words_to_handle_per_proc_dec = (words_counter + NUM_OF_PROC -1) / NUM_OF_PROC;

        for(int p = 0; p < NUM_OF_PROC; p++)
        {
            int proc_begin = p * words_to_handle_per_proc_dec;
            int proc_end   = (p+1) * words_to_handle_per_proc_dec;
            if(proc_end > words_counter) proc_end = words_counter;
            if(proc_begin >= proc_end) break;

            pid_t pid = fork();
            if(pid < 0)
            {
                perror("fork");
                exit(1);
            }
            else if(pid == 0)
            {
                for(int w = proc_begin; w < proc_end; w++)
                {
                    int wstart = start_index_word[w];
                    int wlen = length_word[w];

                    char *enc_word = shm_ptr + wstart;

                    
                    char *perm_line = shm_ptr + enc_size + perm_start[w];
                    char *perm_str = malloc(perm_length[w] + 1);
                    strncpy(perm_str, perm_line, perm_length[w]);
                    perm_str[perm_length[w]] = '\0';

                    
                    int *perm = malloc(wlen * sizeof(int));
                    int idx = 0;
                    for(int i=0; i<wlen; i++)
                    {
                        perm[i] = atoi(&perm_str[idx]);
                        while(perm_str[idx] != ' ' && perm_str[idx] != '\n' && perm_str[idx] != '\0') idx++;
                        if(perm_str[idx] == ' ') idx++;
                        else break;
                    }

                    
                    int *inv_perm = malloc(wlen * sizeof(int));
                    for(int i=0; i<wlen; i++)
                    {
                        inv_perm[perm[i]] = i;
                    }

                    
                    char *dec_word = dec_area + dec_offset[w];
                    for(int i=0; i<wlen; i++)
                    {
                        dec_word[i] = enc_word[inv_perm[i]];
                    }
                    dec_word[wlen] = '\n';

                    free(perm_str);
                    free(perm);
                    free(inv_perm);
                }
                _exit(0);
            }
        }

        for(int p=0; p<NUM_OF_PROC; p++)
        {
            wait(NULL);
        }

        for(int w=0; w<words_counter; w++)
        {
            char *src_dec = dec_area + dec_offset[w];
            int  length_dec = length_word[w] + 1;
            write(fd_decrypted, src_dec, length_dec);
        }

        close(fd_decrypted);

        free(dec_offset);
        free(perm_start);
        free(perm_length);
        free(start_index_word);
        free(length_word);

        munmap(shm_ptr, total_size);
        shm_unlink(shm_name);
    }
    return 0;
}

