#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <fcntl.h>

#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <sys/mman.h>


void check_args(int arguments_count, char *argv[])  // valid call checker
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

int main(int argc, char *argv[]) // MAIN
{
    check_args(argc, argv);
    
    srand(time(NULL));
    
    // the file to encrypt/decrypt
    int fd_in = open(argv[1], O_RDONLY);
    can_open_file(fd_in);
    
    
    struct stat st_file_in;
    if(fstat(fd_in, &st_file_in) < 0)
    {
    	const char *err_msg = "Cannot get information about the file.\n";
    	write(STDERR_FILENO, err_msg, strlen(err_msg));
    	exit(1);
    }
    
    // the file cannot be empty
    check_file_size(st_file_in.st_size);
    
    
    //memory object
    char shm_name[] = "shm_words";
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    can_open_mem_obj(shm_fd);
    
    
    size_t shm_size = st_file_in.st_size;
    
    //dimension for obj
    if(ftruncate(shm_fd, shm_size) < 0)
    {
    	perror(NULL);
    	const char *err_msg = "An error occured while increasing/decreasing object dimension.\n";
    	write(STDERR_FILENO, err_msg, strlen(err_msg));
    	shm_unlink("shm_words");
    	exit(1);
    }
    
    //load
    char *shm_ptr = mmap(0, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm_ptr == MAP_FAILED)
    {
    	perror(NULL);
    	const char *err_msg = "Cannot map memory object.";
    	write(STDERR_FILENO, err_msg, strlen(err_msg));
    	shm_unlink(shm_name);
    	exit(1);
    }
    
    
    if(argc == 2) //case 1 : encrypt file.txt  (encryption)
    {
    	
    }   
    else if(argc == 3) //case 2 : encrypt file.txt permuations.txt (decryption)
    {
    
    } 
    return 0;
}

