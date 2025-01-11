#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <fcntl.h>

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

int main(int argc, char *argv[])
{
    check_args(argc, argv);
    
    
    // the file to encrypt/decrypt
    int fd_in = open(argv[1], O_RDONLY);
    if(fd_in == -1)
    {
    	const char *err_msg = "File couldn't be opened.\n";
    	write(STDERR_FILENO, err_msg, strlen(err_msg));
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

