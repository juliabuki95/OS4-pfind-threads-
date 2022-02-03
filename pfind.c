
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <linux/limits.h>
#include <syscall.h>


typedef struct node_file{
    char *name_folder;
    struct node_file *next;
}node;
typedef struct Queue_files{
    node *head;
    node *tail;
    int size;
}Queue;

pthread_mutex_t q_lock;
pthread_mutex_t match_lock;
pthread_mutex_t idle_lock;
pthread_mutex_t err_lock;
pthread_mutex_t threads_create_lock;
pthread_mutex_t threads_not_finnished_lock;
pthread_cond_t q_cond;
pthread_cond_t main_cond;
pthread_cond_t idle_cond;
pthread_cond_t threads_create_cond;
pthread_cond_t threads_not_finnished_cond;

int existing_threads=0;
int n=0;
int idle=0;
int error=0;
int done=0;
Queue *queue;
char *search;
int match=0;

void queue_init(){
    queue->size=0;
    queue->head=NULL;
    queue->tail=0;
}

//queue func
int push(char *path){
    node *folder= (node*)malloc(sizeof(node));
    if(folder==NULL){
        return -1;
    }
    folder->name_folder=path;
    if(queue->size==0){
        queue->head=folder;
        queue->tail=folder;
    }
    else{
        queue->head->next=folder;
        queue->head=folder;
    }
    folder->next=NULL;
    queue->size++;
    return 0;
}

char* pop(){

    node *node1=queue->tail;
    queue->tail=queue->tail->next;

    queue->size--;
    char *path=node1->name_folder;
    if(queue->size==0){
        queue_init();
    }
    return path;
}

//locks


void lock_cond_init(){
    if(pthread_mutex_init(&q_lock,NULL)||
    pthread_mutex_init(&err_lock,NULL)||
    pthread_mutex_init(&match_lock,NULL)||
    pthread_mutex_init(&idle_lock,NULL)||
    pthread_mutex_init(&threads_create_lock,NULL) ||
    pthread_cond_init(&q_cond, NULL)||
    pthread_cond_init(&main_cond, NULL)||
    pthread_cond_init(&idle_cond, NULL)||
    pthread_cond_init(&threads_create_cond, NULL)||
    pthread_mutex_init(&threads_not_finnished_lock, NULL)||
    pthread_cond_init(&threads_not_finnished_cond, NULL)){
        perror("condition or lock initialization failed");
        exit(1);
    }
}
void push_to_q(char* path){
    DIR *dir=opendir(path);
    DIR *dir_perm;
    char *add_dir;
    struct dirent *entry;
    struct stat st;
    int status;

    if(dir==NULL){
        fprintf(stderr,"Cant open dir %s\n",path);
        pthread_mutex_lock(&err_lock);
        error++;
        if(idle+error==n) {
            done = 1;
            pthread_cond_broadcast(&threads_not_finnished_cond);
        }
        pthread_mutex_unlock(&err_lock);


        pthread_exit(NULL);
    }
    while((entry=readdir(dir))!=NULL){
        if(strcmp(entry->d_name,".")==0||strcmp(entry->d_name,"..")==0){
            continue;
        }
        add_dir=(char*)calloc(PATH_MAX,sizeof(char));
        //creating path of dir
        strcpy(add_dir,path);
        strcpy((add_dir+strlen(path)),"/");
        strcpy((add_dir+strlen(path)+1),entry->d_name);

        if(stat(add_dir,&st)==-1){
            fprintf(stderr, "Error in stat %s\n", add_dir);
            free(add_dir);
            closedir(dir);
           pthread_mutex_lock(&err_lock);
            error++;
            if(idle+error==n) {
                done = 1;
                pthread_cond_broadcast(&threads_not_finnished_cond);
            }
            pthread_mutex_unlock(&err_lock);

            pthread_exit(NULL);
        }
        //if dir
        if(S_ISDIR(st.st_mode)){
            dir_perm=opendir(add_dir);
            //permision check
            if((dir_perm==NULL)||(errno==EACCES)){
                printf("Directory %s: Permission denied.\n", add_dir);
                free(add_dir);
                closedir(dir_perm);
                continue;
            }
            //pushing to queue
            pthread_mutex_lock(&q_lock);
            status = push(add_dir);
            pthread_mutex_unlock(&q_lock);
            //check if push success
            if(status==-1){
                fprintf(stderr,"error in pushing to queue");
                closedir(dir_perm);
                pthread_mutex_lock(&err_lock);
                error++;
                if(idle+error==n) {
                    done = 1;
                    pthread_cond_broadcast(&threads_not_finnished_cond);
                }
                pthread_mutex_unlock(&err_lock);

                pthread_exit(NULL);
            }
            closedir(dir_perm);
            pthread_cond_signal(&q_cond);
        }
        //if file check match
        else if(strstr(entry->d_name,search)){
            printf("%s\n",add_dir);
           pthread_mutex_lock(&match_lock);
            match++;
            pthread_mutex_unlock(&match_lock);

        }

    }
    closedir(dir);


}

void *search_dir(void *t){

    char *path;
    pthread_mutex_lock(&threads_create_lock);
    existing_threads++;
    if(existing_threads==n){
        //free main
        pthread_cond_signal(&main_cond);
        pthread_cond_wait(&threads_create_cond,&threads_create_lock);
    }
    while(existing_threads!=n){
        //wait for all threads
        pthread_cond_wait(&threads_create_cond,&threads_create_lock);
    }
    pthread_mutex_unlock(&threads_create_lock);

    //threads execution
    while(1){

        pthread_mutex_lock(&q_lock);

        while(queue->size==0 && !done){
            //queue empty
            pthread_mutex_lock(&idle_lock);
            idle++;
            if(idle+error==n){
                //queue empty and all threads either waiting or ended
                done=1;
                //wake up main
                pthread_cond_broadcast(&threads_not_finnished_cond);
                pthread_mutex_unlock(&idle_lock);
            }
            else{
                pthread_mutex_unlock(&idle_lock);
                pthread_cond_wait(&q_cond,&q_lock);
                //thread signaled one thread that queue unempty
                pthread_mutex_lock(&idle_lock);
                idle--;
                pthread_mutex_unlock(&idle_lock);
            }

        }
        //queue unempty or execution done
        if(done==1){
            pthread_mutex_unlock(&q_lock);
            break;
        }
        path=pop(queue);
        pthread_mutex_unlock(&q_lock);
        push_to_q(path);
        free(path);
        }
    pthread_exit(NULL);

}
int main(int argc, char *argv[]) {
    //check number of arguments
    if(argc!=4){
        perror("incorredt number of arguments");
        exit(1);
    }
    //check if root can be searched
    char *root;
    root= (char*)calloc(PATH_MAX,sizeof(char));
    strcpy(root,argv[1]);

    DIR *dir= opendir(root);
    if(dir==NULL && (errno==EACCES)){
        printf("Directory %s: Permission denied.\\n",root);
        exit(1);
    }
    closedir(dir);
    //queue init
    queue=(Queue*) malloc(sizeof(Queue));
    if(queue==NULL){
        perror("Queue memory allocation failed");
        exit(1);
    }
    queue_init();
    push(root);
    search=argv[2];
    n=atoi(argv[3]);
    int tc;

    pthread_t *threads=(pthread_t*) malloc(sizeof (pthread_t)*n);
    //threads creation
    for(int i=0;i<n;i++){
        tc=pthread_create(&threads[i],NULL,search_dir,NULL);
        if(tc!=0){
            perror("failed creating thread");
            exit(1);
        }
    }

    pthread_mutex_lock(&threads_create_lock);
    while(existing_threads<n){
        //wait for signal from last thread
        pthread_cond_wait(&main_cond,&threads_create_lock);
    }
    //wake up threads
    pthread_cond_broadcast(&threads_create_cond);
    pthread_mutex_unlock(&threads_create_lock);
    pthread_cond_broadcast(&threads_create_cond);
    pthread_mutex_lock(&threads_not_finnished_lock);
    while((done==0)){
        //wait for threads to be done
        pthread_cond_wait(&threads_not_finnished_cond,&threads_not_finnished_lock);
    }
    pthread_mutex_unlock(&threads_not_finnished_lock);
    //wake up all locked threads to die
    pthread_cond_broadcast(&q_cond);
    for (int i = 0; i < n; i++)
        pthread_join(threads[i], NULL);
    printf("Done searching, found %d files\n",match);

    if(error==0){
        exit(0);
    }
    exit(1);




}
