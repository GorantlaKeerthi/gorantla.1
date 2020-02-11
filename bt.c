#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <sys/dir.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>

struct node {
  char * str;
  struct node* next;
};

struct queue {
  struct node * front;  //first queue node
  struct node * back;   //last queue node
  int num_nodes;        //count of nodes in queue
};

//Program parameters
static int follow_symlinks = 0;
static int print_filetype = 0;
static int print_perms = 0;
static int print_links = 0;
static int print_userid = 0;
static int print_grpid = 0;
static int size_in_units = 0;
static int print_last_mtime = 0;
static char * root = NULL;

static char * perror_prefix = NULL; //error string
static char *bt_filename = NULL;  //program name

static struct queue queue;  //queue of dirs

static int is_empty(struct queue * q){
  return (q->num_nodes == 0);
}

//Add a node to queue
static int enqueue(struct queue * q, const char * str){

  struct node * qn = (struct node *) calloc(1, sizeof(struct node));
  if(qn == NULL){
    perror(perror_prefix);
    return -1;
  }
  qn->str = strdup(str);
  qn->next = NULL;

  if(is_empty(q)){ //queue is empty
    q->front = q->back = qn;
  }else{
    //push node at the end of queue
    q->back->next = qn;
    q->back = qn;
  }
  q->num_nodes++;

  return 0;
};

//Take first node from queue
static char * dequeue(struct queue * q){
  char * str = NULL;

  if(is_empty(q) == 0){

    struct node* qn = q->front;
    q->front = q->front->next;
    if(q->front == NULL){ //if we have emptied queue
      q->back = NULL;  //clear the back too
    }

    str = qn->str;
    free(qn);
    q->num_nodes--;
  }

  return str;
}

//Return the unit of file size
static char get_size_unit(unsigned int size){
  int index=0;
  const char unit[] = "bKMG?";
  while((size > 1024) && (index < sizeof(unit))){
      size /= 1024;
      ++index;
  }
  return unit[index];
}

//Visit a node and print information about it
static int visit(const char * path){

  struct stat st;
  int rv;

  if(follow_symlinks){
    rv = stat(path, &st);
  }else{
    rv = lstat(path, &st);
  }

  if(print_filetype){
    switch(st.st_mode & S_IFMT){
      case S_IFSOCK:
        putchar('s'); break;
      case S_IFLNK:
        putchar('l'); break;
      case S_IFREG:
        putchar('-'); break;
      case S_IFBLK:
        putchar('b'); break;
      case S_IFCHR:
        putchar('c'); break;
      case S_IFDIR:
        putchar('d'); break;
      case S_IFIFO:
        putchar('|'); break;
      default:
        putchar('?'); break;
    }
  }


  if(print_perms){  //if we have option -p
    char p[10];
    memset(p, '-', 9);
    p[10] = '\0';

    if(st.st_mode & S_IRUSR) p[0] = 'r';
    if(st.st_mode & S_IWUSR) p[1] = 'w';
    if(st.st_mode & S_IXUSR) p[2] = 'x';

    if(st.st_mode & S_IRGRP) p[3] = 'r';
    if(st.st_mode & S_IWGRP) p[4] = 'w';
    if(st.st_mode & S_IXGRP) p[5] = 'x';

    if(st.st_mode & S_IROTH) p[6] = 'r';
    if(st.st_mode & S_IWOTH) p[7] = 'w';
    if(st.st_mode & S_IXOTH) p[8] = 'x';

    printf("%s ", p);
  }


  if(print_links){
    printf("%i ", (int)st.st_nlink);
  }

	if (print_userid){
    struct passwd * pwent = getpwuid(st.st_uid);
    if(pwent == NULL){
      perror(perror_prefix);
      return -1;
    }
  	printf("%-10s ", pwent->pw_name);  //username
  }


	if(print_grpid){
    struct group *grp = getgrgid(st.st_gid);
    if(grp == NULL){
      perror(perror_prefix);
      return -1;
    }
  	printf("%-10s ", grp->gr_name);
  }

  if(size_in_units){
    char unit = get_size_unit(st.st_size);
    if(unit != 'b'){
      printf("%9u%c ", st.st_size, unit);
    }else{
      printf("%10u ", st.st_size);
    }
  }else{
    printf("%10u ", st.st_size);
  }

  if(print_last_mtime){
    struct tm time;
    char buf[256];

  	localtime_r(&st.st_mtime, &time);
  	strftime(buf, sizeof(buf), "%h %d, %Y", &time);
  	printf("%s ", buf);
  }

  printf("%s\n", path);

  return 0;
};

//Check if a path is a directory
static int is_directory(const char * path){
  struct stat st;
  int rv;

  if(follow_symlinks){
    rv = stat(path, &st);
  }else{
    rv = lstat(path, &st);
  }

  if(rv == -1){
    perror(perror_prefix);
    return -1;
  }

  //before we add to queue, make sure we can traverse it
  if(S_ISLNK(st.st_mode)){  //if its a symbolic link
    return follow_symlinks; //option will determine if we follow the dir
  }

  if (!S_ISDIR(st.st_mode)){
    return 0;
  }
  return 1;
}

//List a directory, putting into queue, all dirs we found
static int traversal(const char * dirname){

	struct dirent *dent;
	char path[PATH_MAX];

  DIR * dir = opendir(dirname);
	if (dir == NULL) { //if opendir fails
		perror(perror_prefix);
		return -1;
	}

  //traverse the whole directory
	while ((dent = readdir(dir))) {

		//skip current dir and previous dir
		if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..")){
			continue;
    }

    //concatenate dirname and entry name
    snprintf(path, PATH_MAX, "%s/%s", dirname, dent->d_name);

    if (is_directory(path)){
      enqueue(&queue, path);  //save the directory to queue
    }
    visit(path);
	}
	closedir(dir);

  return 0;
}

//Traverse through root in breath-first search
static int breadthfirst(){

  visit(root);  //print top node info

  //traverse each node in the queue
  while(!is_empty(&queue)){
    char * next = dequeue(&queue);
    traversal(next);
    free(next);
  }
  return 0;
}

static void print_help_menu(){
  printf("# Usage: %s [-h] [-L -d -g -i -p -s -t -u | -l] [dirname]\n", bt_filename);
  printf("-h \t Show help information\n");
  printf("-d \t Show the time of last modification (default no)\n");
  printf("-L \t Follow symbolic links (default no)\n");

  printf("# Print format options:\n");
  printf("-t \t information on file type (default no)\n");
  printf("-p \t permission bits (default no)\n");
  printf("-i \t the number of links to file in inode table (default no)\n");
  printf("-u \t the uid associated with the file (default no)\n");
  printf("-g \t the gid associated with the file (default no)\n");
  printf("-s \t the size of file in bytes (default no)\n");

  printf("# Shotcut options:\n");
  printf("-l \t Enables options -t, -p, -i, -u, -g, -s\n");
  exit(0);
}

static void parse_arguments(const int argc, char * const argv[]){

  //check parameters and set our options
  int opt;
  while((opt = getopt(argc,argv, "hLdipstgul")) != -1){
    switch(opt){
      case 'h': print_help_menu();  break;
      case 'L': follow_symlinks = 1;  break;
      case 'd': print_last_mtime = 1;  break;
      case 'i': print_links = 1;  break;
      case 'p': print_perms = 1;  break;
      case 's': size_in_units = 1;  break;
      case 't': print_filetype = 1;  break;
      case 'g': print_grpid = 1;  break;
      case 'u': print_userid = 1;  break;
      case 'l':
        print_filetype = print_perms =
        print_links    = print_userid =
        print_grpid    = size_in_units = 1;
        break;

      default:
        fprintf(stderr, "Error: Unknown option %c\n", opt);
        exit(EXIT_FAILURE);
        break;
    }
  }

  //If the user does not specify dirname
  if(argc == optind){
    //allocate space for root
    root = (char*) malloc(sizeof(char)*PATH_MAX);
    if(root == NULL){
      perror(perror_prefix);
      exit(EXIT_FAILURE);
    }

    //get current directory
    if(getcwd(root, PATH_MAX) == NULL){
      perror(perror_prefix);
      exit(EXIT_FAILURE);
    }

  }else{

    //check if use has entered a directory name
    if(is_directory(argv[optind]) == 0){
      fprintf(stderr, "%s: '%s' is not a dir\n", perror_prefix, argv[optind]);
      exit(EXIT_FAILURE);
    }

    root = strdup(argv[optind]);
    if(root == NULL){
      perror("strdup");
      exit(EXIT_FAILURE);
    }
  }
}

static int set_perror_prefix(char * argv0){

  bt_filename = strrchr(argv0, '/') + 1;

  int len = snprintf(NULL, 0, "%s: Error", bt_filename);
  perror_prefix = (char *) malloc(len+1);
  if(perror_prefix == NULL){
    perror("malloc");
    return -1;
  }
  snprintf(perror_prefix, len+1, "%s: Error", bt_filename);
  return 0;
}

int main(const int argc, char * const argv[]){

  set_perror_prefix(argv[0]);

  queue.front = queue.back = NULL;
  queue.num_nodes = 0;

  parse_arguments(argc, argv);

  enqueue(&queue, root);  //save the directory to queue
  breadthfirst();

  free(perror_prefix);

  return 0;
}
