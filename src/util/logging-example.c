#include "logging.h"

#include <stdio.h>
#include <getopt.h>

int main(int argc, char *argv[]) {
    int opt;
    int option_index = 0;
    
    // Command line options
    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"quiet", no_argument, 0, 'q'},
        {"help", no_argument, 0, 1001},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "abchkmsp:P:vq", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'v':
                log_more();
                break;
            case 'q':
                log_less();
                break;
            default:
                print_usage();
        }
    }
    
    logdebug("Copying directory");
    logerror("Error Getting file list for directory");
    loginfo("Successfully copied");
    loglog("Writing some bytes to destination file");
    logmesg("Creating destination file foobar.txt");
    logwarn("Using partition 1");

    logdebug_fmt("Copying directory %s to %s", src_dir, dest_dir);
    logerror_fmt("Error Getting file list for directory: %s", path_start);
    loginfo_fmt("Successfully copied %s to %s", src_path, dest_path);
    loglog_fmt("Writing %u bytes to destination file", bytes_read);
    logmesg_fmt("Creating destination file: %s", dest_path);
    logwarn_fmt("Using partition: %d", partition);

}

