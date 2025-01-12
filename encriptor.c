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
            if(start_of_word != 1) // has bee n set
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
               
            }
        }
    }
    //last word
    if(start_of_word != -1)
    {
        start_index_word[words_counter] = start_of_word;
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
    else if(argc == 3)
    {
        // dec
    }
    return 0;
}

