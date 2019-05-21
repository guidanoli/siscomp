
  // Interpreter & Scheduler
  // Guilherme Dantas

  #include <stdio.h>
  #include <stdlib.h>
  #include <errno.h>
  #include <sys/types.h> 
  #include <unistd.h>
  #include <sys/wait.h>
  #include <stdarg.h>
  #include <string.h>
  #include <pthread.h>
  #include "queue.h"
  
  #define FIRST_QUEUE_ID 0
  #define SMALLEST_QUANTUM 2
  #define IO_BLOCK_TIME 3
  #define N_OF_QUEUES 3
  #define ARG_SIZE 64
  #define QTD_ARGS 16
  #define SEM_KEY 123456789
  
  typedef enum {
    NORMAL,
    IO,
    TERMINATED,
  } proc_status;
      
  typedef struct process_s {
    qnode node;
    proc_status status;
  } process;
  
  typedef struct process_queue_s {
    qhead queue;
    int id;
    int runs_left;
  } procqueue;
  
  typedef struct process_package_s {
    qnode process;
    qhead queue;
  } procpack;
  
  // processes and queues
  qhead proc_queues[N_OF_QUEUES];
  qhead aux_queue = NULL;
  process current_proc;
  procqueue current_queue;
  int processes_count = 0;
  int io_threads = 0;
  
  // semaphore and queues
  int semId = 0;
  qhead signal_queue;
  
  // functions
  int fatal_error(const char * err_msg_format, ...);
  int create_queues(void);
  int init_interpreter();
  void destroy_queues(void);
  int myPow2(int exp);
  int getQueueRuns(int id);
  int getQueueQuantum(int id);
  int getHigherPriorityQueueId(int id);
  int getLowerPriorityQueueId(int id);
  qhead getQueueFromId(int id);
  qhead getUpdatedQueue();
  void setCurrentQueue(int id);
  void forceNextQueue();
  void dummy_handler(int signo);
  void ioHandler(int pid);
  void signalHandler(int signo);
  void * ioThreadFunction(void * info);
  procpack * getCurrentProcessPackage();
  void dump_queues();

  int main(int argc, char ** argv)
  {
    int ret = 0, quantum = 0;
    qhead queue;
    pid_t pid;
    
    /* create queues */
    if((ret=create_queues())!=0) return ret;
    
    /* initialize semaphore */
    if((semId = semCreate(SEM_KEY))==-1) return EXIT_FAILURE;
    if(semInit(semId)==-1) return EXIT_FAILURE;
   
    /* initialize interpreter signal handler */
    signal(SIGUSR2,dummy_handler);
    
    /* parse args from stdin and build queues */
    if((ret=init_interpreter())!=0) return ret;
    
    /* initialize scheduler signal handlers */
    signal(SIGUSR1,signalHandler);
    signal(SIGUSR2,signalHandler);
            
    /* start from queue of highest priority */
    setCurrentQueue(FIRST_QUEUE_ID);
    
    #ifdef _DEBUG
    printf("There are %d processes waiting to be executed.\n",processes_count);
    printf("Starting scheduler...\n");
    #endif
        
    /* main loop */
    while( 1 )
    {
      if( processes_count <= 0 ) break;
      
      // there is no need for mutex here
      // since the processes_count only
      // decreases and it only reads the
      // content of process_count and does
      // not modify it.
      
      if( processes_count != io_threads )
      {
        queue = getUpdatedQueue();
        quantum = getQueueQuantum(current_queue.id);
        #ifdef _DEBUG
        if( qhead_empty(queue) == QUEUE_FALSE )
          printf("The scheduler is dealing with queue #%d.\n",current_queue.id);
        #endif
      }
      
      /* current process loop */
      while( 1 )
      {
        int flag = 0;
        /////////////////////////////////
        // ENTERS CRITICAL REGION
        // Manipulates current process
        // and current queue
        /////////////////////////////////
        enterCR(semId);
        /////////////////////////////////
        if( qhead_empty(queue) == QUEUE_FALSE )
        {
          current_proc.node = qhead_rm(queue);
          current_proc.status = NORMAL;
          pid = qnode_getid(current_proc.node);
          flag = 1;
        }
        /////////////////////////////////
        exitCR(semId);
        /////////////////////////////////
        // EXITS CRITICAL REGION
        /////////////////////////////////        
        if( !flag ) break;
        
        #ifdef _DEBUG
        printf("Scheduling process %d...\n",pid);
        #endif
        
        kill(pid,SIGCONT);
        sleep(quantum); // z z z ...
        kill(pid,SIGSTOP);
        
        #ifdef _DEBUG
        printf("Interrupted process %d.\n",pid);
        #endif
        
        /////////////////////////////////
        // ENTERS CRITICAL REGION
        // All signals have to be treated
        /////////////////////////////////
        enterCR(semId);
        /////////////////////////////////
        while( qhead_empty(signal_queue) == QUEUE_FALSE )
        {
          qnode sig;
          int signo;
          sig = qhead_rm(signal_queue);
          signo = qnode_getid(sig);
          qnode_destroy(&sig);
          if( signo == SIGUSR1 )
          {
            ioHandler(pid);
            current_proc.status = IO;
          }
          if( signo == SIGUSR2 )
          {
            exitHandler(pid);
            current_proc.status = TERMINATED;
          }
        }
        /////////////////////////////////
        exitCR(semId);
        /////////////////////////////////
        // EXITS CRITICAL REGION
        /////////////////////////////////
        
        /////////////////////////////////
        // ENTERS CRITICAL REGION
        // Manipulates current process
        /////////////////////////////////
        enterCR(semId);
        /////////////////////////////////
        if( current_proc.status == NORMAL )
        {
          int new_queue_id = getLowerPriorityQueueId(current_queue.id);
          #ifdef _DEBUG
          printf("Process %d exceeded queue quantum of %d time units .\n",pid,quantum);
          #endif
          if( new_queue_id == current_queue.id )
          {
            qhead_ins(aux_queue,current_proc.node);
            #ifdef _DEBUG
            printf("Process %d will remain in queue #%d.\n", pid,new_queue_id);
            #endif
          }
          else
          {
            qhead_ins(getQueueFromId(new_queue_id),current_proc.node);
            #ifdef _DEBUG
            printf("Process %d will migrate from queue #%d to queue #%d\n",
            pid, current_queue.id, new_queue_id);
            #endif
          }
        }
        /////////////////////////////////
        exitCR(semId);
        /////////////////////////////////
        // EXITS CRITICAL REGION
        /////////////////////////////////
      }
      /////////////////////////////////
      // ENTERS CRITICAL REGION
      // Manipulates auxiliary queue
      /////////////////////////////////
      enterCR(semId);
      /////////////////////////////////
      if( qhead_transfer(aux_queue,queue,QFLAG_TRANSFER_ALL) != QUEUE_OK )
        return fatal_error("An error occurred while managing auxiliary queue\n");
      /////////////////////////////////
      exitCR(semId);
      /////////////////////////////////
      // EXITS CRITICAL REGION
      /////////////////////////////////
    }
    
    #ifdef _DEBUG
    printf("End of scheduling. All processes have been executed.\n");
    #endif
    
    /* safely destroying semaphore */
    semDestroy(semId);
    
    /* safely freeing queues */
    destroy_queues();
    
    /* end scheduler */
	  return EXIT_SUCCESS;
  }
  
  ///////////////////////////////
  // Functions' implementation //
  ///////////////////////////////
  
  int fatal_error(const char * err_msg_format, ...)
  {
    va_list vl;
    va_start(vl, err_msg_format);
    vfprintf(stderr,err_msg_format,vl);
    va_end(vl);
    return EXIT_FAILURE;
  }
  
  int create_queues(void)
  {  
    if( qhead_create(&signal_queue,-1) != 0 )
      return fatal_error("Could not create signal queue.\n");
    if( qhead_create(&aux_queue,-1) != 0 )
      return fatal_error("Could not create auxiliary queue.\n");
    for( int i = 0 ; i < N_OF_QUEUES ; i++ )
      if( qhead_create(proc_queues+i,i) != 0 )
        return fatal_error("Could not create queue #%d.\n",i);
    return 0;
  }

  void destroy_queues(void)
  {
    qhead_destroy(&signal_queue);
    qhead_destroy(&aux_queue);
    for( int i = 0 ; i < N_OF_QUEUES ; i++ )
      qhead_destroy(proc_queues+i);
  }
  
  qhead getQueueFromId(int id)
  {
    return proc_queues[id%N_OF_QUEUES];
  }
  
  qhead getUpdatedQueue()
  {
    if( current_queue.runs_left == 0 )
    {
      #ifdef _DEBUG
      if( processes_count != io_threads && qhead_empty(current_queue.queue) == QUEUE_FALSE )
        printf("Queue #%d has reached its limit of %d cycles.\n",current_queue.id,getQueueRuns(current_queue.id));
      #endif
      forceNextQueue();
    }
    current_queue.runs_left--; // already wastes by calling
    return getQueueFromId(current_queue.id);
  }
    
  void forceNextQueue()
  {
    setCurrentQueue((current_queue.id+1)%N_OF_QUEUES);
  }
  
  void setCurrentQueue(int id)
  {
    /////////////////////////////////
    // ENTERS CRITICAL REGION
    // Manipulates current queue
    /////////////////////////////////
    enterCR(semId);
    /////////////////////////////////
    current_queue.id = id;
    current_queue.runs_left = getQueueRuns(id);
    /////////////////////////////////
    exitCR(semId);
    /////////////////////////////////
    // EXITS CRITICAL REGION
    /////////////////////////////////
  }
  
  int myPow2(int exp) { return 1<<exp; }
  
  int getQueueRuns(int id) { return myPow2(N_OF_QUEUES-id-1); }
  
  int getQueueQuantum(int id) { return myPow2(id)*SMALLEST_QUANTUM; }
    
  // needs to be called inside a semaphore
  procpack * getCurrentProcessPackage()
  {
    procpack * pack = (procpack *) malloc(sizeof(procpack));
    if( pack == NULL ){
      fprintf(stderr,"Could not allocate memory.\n");
      exit(0); //abort program
    }
    pack->process = current_proc.node;
    pack->queue = getQueueFromId(getHigherPriorityQueueId(current_queue.id));
    return pack;
  }
  
  // Adds signal to signal queue
  void signalHandler(int signo)
  {    
    qnode sig;
    enterCR(semId); // enters CR
    qnode_create(&sig,signo);
    qhead_ins(signal_queue,sig);
    exitCR(semId); // exits CR
  }
  
  // needs to be called inside a semaphore
  void ioHandler(int pid)
  {
    void * info;
    pthread_t thread;
    info = (void *) getCurrentProcessPackage(); // inside semaphore
    pthread_create(&thread,NULL,ioThreadFunction,info);
    io_threads++;
    #ifdef _DEBUG
    if( io_threads > processes_count ) // Crash-proof
    {
      printf("#Threads > #Processes ! ! !\n");
      exit(0);
    }
    #endif
  }
  
  // needs to be called inside a semaphore
  void exitHandler(int pid)
  {
    qnode dead_node;
    dead_node = current_proc.node;
    processes_count--;
    qnode_destroy(&dead_node);
    kill(pid,SIGKILL);
    printf("Process %d finished.\n",pid);
    #ifdef _DEBUG
    dump_queues();
    #endif
    if( processes_count > 0 )
    {
      printf("There are %d remaining processes\n",processes_count);
      printf("* %d in queue\n",processes_count-io_threads);
      printf("* %d blocked by IO\n",io_threads);
    }
    else
      printf("No remaining processes\n");
    
  }
  
  void * ioThreadFunction(void * info)
  {
    procpack * pack;
    qnode io_proc;
    qhead new_queue;
    int my_pid;
    pack = (procpack *) info;
    io_proc = pack->process;
    my_pid = qnode_getid(io_proc);
    new_queue = pack->queue;
    //free(info);
    printf("Process %d is blocked by IO.\n",my_pid);
    sleep(IO_BLOCK_TIME); // simulating IO
    /////////////////////////////////
    // ENTERS CRITICAL REGION
    // Manipulates io_process and
    // any of the process queues
    /////////////////////////////////
    enterCR(semId);
    /////////////////////////////////
    qhead_ins(new_queue,io_proc);
    io_threads--;
    printf("Processo %d is no longer blocked by IO.\n",my_pid);
    /////////////////////////////////
    exitCR(semId);
    /////////////////////////////////
    // EXITS CRITICAL REGION
    /////////////////////////////////
    pthread_exit(NULL); // end thread
  }
  
  int getHigherPriorityQueueId(int id)
  {
    if(id <= 0) return 0;
    return (id-1)%N_OF_QUEUES;
  }
  
  int getLowerPriorityQueueId(int id)
  {
    if(id >= N_OF_QUEUES-1) return N_OF_QUEUES-1;
    return (id+1)%N_OF_QUEUES;
  }
   
  void dummy_handler(int signo) {}
  
  // needs to be inside a semaphore!!
  void dump_queues()
  {
    for(int i = 0 ; i < N_OF_QUEUES ; i++ )
    {
      qhead f = proc_queues[i];
      qhead aux;
      printf("Fila #%d: %s\n",i,(qhead_empty(f)==QUEUE_OK)?"empty":"not empty");
      qhead_create(&aux,-1);
      while( qhead_empty(f) == QUEUE_FALSE )
      {
        qnode n = qhead_rm(f);
        qhead_ins(aux,n);
        printf("- %d\n",qnode_getid(n));
      }
      qhead_transfer(aux,f,QFLAG_TRANSFER_ALL);
      qhead_destroy(&aux);
    }
  }
  
  int init_interpreter()
  {
    char c;
    pid_t pid;
    /* parse args from stdin */
    while((c = fgetc(stdin)) == 'e')
    {
      char prog[ARG_SIZE] = "";
      int raj[QTD_ARGS];
      int qt_raj = 1;
      
      if( scanf("xec %s (%d",prog,raj) != 2 )
      { 
        return fatal_error("Bad string format.\n");
      }
      while((c = fgetc(stdin)) == ',')
      {
        if( scanf("%d",raj+qt_raj) != 1 )
        {
          return fatal_error("Bad string format.\n");
        }
        qt_raj++;
      }
      if( c != ')' )
      {
        return fatal_error("Bad string format.\n");
      }
      scanf(" ");
      
      if((pid = fork()) == 0)
      {
        char * args[QTD_ARGS];
        char buffer[QTD_ARGS*(ARG_SIZE+1)+1] = "";
        for( int i = 0 ; i < qt_raj+2 ; i++ )
        {
          args[i] = (char *) malloc(sizeof(char)*ARG_SIZE);
          if( i == 0 ) strcpy(args[0],prog);
          else if( i < qt_raj+1 ) sprintf(args[i],"%d",raj[i-1]);
          if( i < qt_raj+1 )
          {
            strcat(buffer,args[i]);
            strcat(buffer," ");
          }
        }
        args[qt_raj+1] = NULL;
        printf("Child process %d executing... %s\n",getpid(),buffer);
        if( execv(*args,args) == -1 )
        {
          return fatal_error("Could not execute process %d.\n",getpid());
        }
        exit(0);
      }
      else
      {
        sleep(100);
        kill(pid,SIGSTOP);
        qnode process;
        if( qnode_create(&process,pid) != 0 )
        {
          fprintf(stderr,"Could not allocate memmory.\n");
          return EXIT_FAILURE;
        }
        qhead_ins(proc_queues[FIRST_QUEUE_ID],process);
        sleep(1);
        processes_count++;
        #ifdef _DEBUG
        printf("Child process %d inserted in queue #%d\n",pid,FIRST_QUEUE_ID);
        #endif
      }
    } /* end parsing */
    return EXIT_SUCCESS;
  }
  
  
  
