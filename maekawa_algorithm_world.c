#define DEBUG 1 // set  to 0 to hide debug messages
#define HEAP_CAPACITY 20 //For waiting_queue capacity at each node
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <omp.h>
#include <time.h>
#include <unistd.h>
//For printing debug messages inside printd() if DEBUG is set to 1
#define printd(fmt, ...) \
            do { if (DEBUG) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)

enum Message {REQUEST,      //request to enter CS
              YES,          //voting for the request
              RELEASE,      // message sent by i to every one in district once it comes out of its CS
              INQUIRE,      // message from i to candidate to check whether candidate has entered its CS.
              RELINQUISH    //candidate relinquishes the vote obtained by it in favour of a candidate with lower timestamp to avoid deadlock
              };

typedef struct message_s{
    //enum Message msg;
    int msg;
    int seq_no;
    int id;
}message;

typedef struct s_heap{
    message msg;
    int rts;
}heap;
//creates waiting waiting_queue
int heap_size=0;
heap waiting_queue[HEAP_CAPACITY];
int inCS=0;
//for heap operations
int parent(int i) { return (i-1)/2; }
int left(int i) { return (2*i + 1); }
int right(int i) { return (2*i + 2); }
void swap(heap *x, heap *y)
{
    heap temp = *x;
    *x = *y;
    *y = temp;
}
// Inserts a new key 'k'
void printHeap(){
    for(int i=0;i<heap_size;++i){
        printf("[printHeap]seq_no=%d, id=%d, msg=%d\n",waiting_queue[i].msg.seq_no,waiting_queue[i].msg.id,waiting_queue[i].msg.msg);
    }
}
void insertKey(message m)
{
    if (heap_size == HEAP_CAPACITY)
    {
        printf("\nOverflow: Could not insertKey\n");
    }

    // First insert the new key at the end
    heap_size++;
    int i = heap_size - 1;
    waiting_queue[i].msg = m;
    waiting_queue[i].rts = m.seq_no;
    // Fix the min heap property if it is violated
    while (i != 0 && (waiting_queue[parent(i)].rts > waiting_queue[i].rts || (waiting_queue[parent(i)].rts == waiting_queue[i].rts && waiting_queue[parent(i)].msg.id > waiting_queue[i].msg.id)))
    {
       swap(&waiting_queue[i], &waiting_queue[parent(i)]);
       i = parent(i);
    }
}

void MinHeapify(int i)
{
    int l = left(i);
    int r = right(i);
    int smallest = i;
    if (l < heap_size && waiting_queue[l].rts < waiting_queue[i].rts)
        smallest = l;
    if(l < heap_size && waiting_queue[l].rts == waiting_queue[i].rts && waiting_queue[i].msg.id > waiting_queue[l].msg.id)
        smallest=l;
    if(r < heap_size && waiting_queue[r].rts == waiting_queue[smallest].rts && waiting_queue[smallest].msg.id > waiting_queue[r].msg.id)
        smallest = r;
    if (r < heap_size && waiting_queue[r].rts < waiting_queue[smallest].rts)
        smallest = r;
    if (smallest != i)
    {
        swap(&waiting_queue[i], &waiting_queue[smallest]);
        MinHeapify(smallest);
    }
}

message extractMin()
{
    if (heap_size <= 0)
        printf("Trying to extractMin from empty waiting_queue\n");
    if (heap_size == 1)
    {
        heap_size--;
        return waiting_queue[0].msg;
    }
    // Store the minimum value, and remove it from heap
    message root = waiting_queue[0].msg;
    waiting_queue[0] = waiting_queue[heap_size-1];
    heap_size--;
    MinHeapify(0);
    return root;
}

MPI_Datatype mpi_message;
int printRecvMessage(message m,int world_rank){
    char *msg_texts[]={"REQUEST","YES","RELEASE","INQUIRE", "RELINQUISH"};
    printf("Received Message %d(%s) at %d from %d, seq= %d\n",m.msg,msg_texts[m.msg],world_rank,m.id,m.seq_no);
    return 0;
}
int printSentMessage(message m,int rank){
    char *msg_texts[]={"REQUEST","YES","RELEASE","INQUIRE", "RELINQUISH"};
    printf("Sent Message %d(%s) from %d to %d, seq= %d\n",m.msg,msg_texts[m.msg],m.id,rank,m.seq_no);
    return 0;
}
int printMessage(message m,int world_rank){
    char *msg_texts[]={"REQUEST","YES","RELEASE","INQUIRE", "RELINQUISH"};
    printf("[WQ]Received Message %d(%s) from %d to %d, seq= %d\n",m.msg,msg_texts[m.msg],m.id,world_rank,m.seq_no);
    return 0;
}
int createVotingDistricts(char *filename,int n,int **vd){
    printd("DEBUG : Inside createVotingDistricts()");
    int c,row=0,col=0,nodes,k,temp=0;
    //vd=(int *) malloc (sizeof(int) * n*(n-1))
    FILE *file;
    file = fopen(filename, "r");
    if (file) {
        printd("DEBUG : voting district configuration file opened.\n");
        while ((c = getc(file)) != '\n'){
            switch(c){
                case 'x':
                case 'X':
                    nodes=temp;
                    temp=0;
                    break;
                default:
                    temp*=10;
                    temp+=(c-'0');
            }
        }
        k=temp;
        printf("Number of nodes = %d\nNumber of nodes in a voting district = %d",nodes,k);
        while ((c = getc(file)) != EOF){
            //putchar(c);
            switch(c){
                case '\n':
                    row++;
                    col=0;
                    break;
                case ' ':
                    col++;
                    break;
                default:
                    vd[row][col]=c-'0';
                    //printf("%d:%d\n",world_rank,vd[row][col]);
            }
        }
        fclose(file);
    }
    return 0 ;
}
int enterCS(int world_rank,int *seq,int k,MPI_Datatype mpi_message,int *voting_district){
    int yes_votes=0,      // # of processes voted yes to i
        sender_rank
        ;
    (*seq)++;
    //Broadcast (REQUEST, Ts, i) in voting_district
    message send_message,recv_message;
    MPI_Status status;
    send_message.msg=REQUEST;
    send_message.seq_no=*seq;
    send_message.id=world_rank;
    for(int i=0;i<k;++i){
            MPI_Send(&send_message,1,mpi_message,voting_district[i],1,MPI_COMM_WORLD);
            printSentMessage(send_message,voting_district[i]);
    }
    while (yes_votes<k) {
        //receive message
        printd("%d:yes_votes=%d, waiting to get %d yes_votes\n",world_rank,yes_votes,k);
        MPI_Recv(&recv_message,1,mpi_message,MPI_ANY_SOURCE,0,MPI_COMM_WORLD,&status);
        printf("%d:Entry Section Testpoint 1",world_rank);
        printRecvMessage(recv_message,world_rank);
        //process message
        //sender_rank=status.MPI_SOURCE;
        sender_rank=recv_message.id;
        switch(recv_message.msg){
            case YES:
                yes_votes++;
                break;
            case INQUIRE:
                //send RELINQUISH message to sender
                send_message.msg=RELINQUISH;
                send_message.seq_no=recv_message.seq_no;//added while debugging
                send_message.id=world_rank;
                MPI_Send(&send_message,1,mpi_message,sender_rank,1,MPI_COMM_WORLD);
                printSentMessage(send_message,sender_rank);
                yes_votes--;
                break;
            default:
                printf("%d:WARNING - THIS WARNING IS UNEXPECTED! inside enterCS\n",world_rank);
                printf("%d:Got Message with message=%d, seq_no=%d, world_rank=%d at node %d\n",
                        world_rank,recv_message.msg,recv_message.seq_no,recv_message.id,world_rank);
        }

    }
    inCS=1;
    printf("%d: reached end of Entry Section\n",world_rank);
    return 0;
}
int criticalSection(int rank){
    printf("%d:*************************\n",rank);
    printf("%d:Critical Section Executed\n",rank);
    printf("%d:*************************\n",rank);
    sleep(5);
    return 0;
}
int exitCS(int world_rank,int k,MPI_Datatype mpi_message,int *voting_district){
    printf("ES:Entered Exit Section");
    message send_message;
    send_message.msg=RELEASE;
    send_message.seq_no=-1;
    send_message.id=world_rank;
    //for (∀r ∈ Si), Send(RELEASE, i) to r
    for(int i=0;i<k;++i){
        //if(voting_district[i]!=world_rank){
            printd("%d: Exit Section before Sending RELEASE to %d\n",world_rank,voting_district[i]);
            MPI_Send(&send_message,1,mpi_message,voting_district[i],1,MPI_COMM_WORLD);
            printSentMessage(send_message,voting_district[i]);
        //}
    }
    inCS=0;
    printf("ES:Reached Exit of Exit Section");
    return 0;
}
int compare(const void *s1, const void *s2){
    message *m1 = (message *)s1;
    message *m2 = (message *)s2;
    int compared=(m1->seq_no - m2->seq_no);
    if(compared)
        return compared;
    return m1->id - m2->id;
}
int messageHandlingSection(int world_rank,int *seq,int k,MPI_Datatype mpi_message,int *voting_district){
    //MPI_Comm voting_district=comm_district[world_rank];
    message send_message, //For sending of messages
            recv_message, //For receiving messages
            min_message     //Message with minimum timestamp
            ;
    MPI_Status status;
    int local_rank_lookup[k],   //for world rank to district rank conversion
        voted_candidate=-1,//the candidate for whom the node is voted
        candidate_seq=-1, //Sequence number(time stamp) of Candidate for which the node has voted
        sender_rank=-1 //For storing sender's rank(of received message)
        ;
    bool have_voted=false ,     //true, if i has already voted for a candidate process
         have_inquired=false  //true, if i has tried to recall a voting (initially it is false)
        ;
   
    while(1){
        printd("%d:MHS Waiting to Receive\n",world_rank);
        MPI_Recv(&recv_message,1,mpi_message,MPI_ANY_SOURCE,1,MPI_COMM_WORLD,&status);
        printd("%d:MHS Msg Received\n",world_rank);
        printRecvMessage(recv_message,world_rank);
        //process message
        sender_rank=status.MPI_SOURCE;

        switch(recv_message.msg){
            case REQUEST:
                if(!have_voted){
                    //Send (YES,i) to sender
                    send_message.msg=YES;
                    send_message.seq_no=recv_message.seq_no;
                    send_message.id=world_rank;
                    MPI_Send(&send_message,1,mpi_message,sender_rank,0,MPI_COMM_WORLD);//voting_district);
                    printSentMessage(send_message,sender_rank);
                    voted_candidate=sender_rank;
                    candidate_seq=recv_message.seq_no;
                    have_voted=true;
                }
                else{
                    //waiting_queue[top++]=recv_message;
                    insertKey(recv_message); // adds to waiting_queue(heap)
                    printf("%d:Message from %d added to waiting_queue of %d",world_rank,recv_message.id,world_rank);
                    printd("%d: waiting_queue size= %d",world_rank,heap_size);
                    printHeap();
                    //Add the rank of the sender in local_rank_lookup table
                    local_rank_lookup[recv_message.id]=sender_rank;
                    //if seq no of current msg> voted seq or (both seq_no is equal and rank of voted>current msg rank)
                    if((recv_message.seq_no<candidate_seq || (recv_message.seq_no==candidate_seq && recv_message.id<voted_candidate))&& !have_inquired){
                        // Send(INQUIRE,i, Candidate_TS) to Candidate
                        send_message.msg=INQUIRE;
                        //send_message.seq_no=recv_message.seq_no;
                        send_message.seq_no=candidate_seq;
                        send_message.id=world_rank;
                        //if voted_candidate==self, then forward to EntrySection.else send with tag=1
                        if(voted_candidate==world_rank){
                            if(inCS==0)
                                MPI_Send(&send_message,1,mpi_message,voted_candidate,0,MPI_COMM_WORLD);
                        }
                        else
                            MPI_Send(&send_message,1,mpi_message,voted_candidate,1,MPI_COMM_WORLD);
                        printSentMessage(send_message,voted_candidate);
                        have_inquired=true;
                    }
                }
                break;
            case RELINQUISH:
                //waiting_queue[top++]=recv_message;
                insertKey(recv_message); // adds message to waiting_queue
                printf("%d:Message from %d added to waiting_queue of %d",world_rank,recv_message.id,world_rank);
                //Sort the waiting waiting_queue
                //qsort(waiting_queue,top,sizeof(message), compare);
                //Remove(s,rts) from Waiting_Q such that rts is minimum
                min_message=extractMin(); //gets message with min timestamp from waiting_queue
                //min_message=waiting_queue[top--];
                //Send(YES,i) to s
                send_message.msg=YES;
                send_message.seq_no=recv_message.seq_no;
                send_message.id=world_rank;
                //after debugging, changing 4th argument from sender_rank to min_message.id
                MPI_Send(&send_message,1,mpi_message,min_message.id,0,MPI_COMM_WORLD);
                printSentMessage(send_message,min_message.id);
                //candidate:=s.
                voted_candidate=min_message.id;//local_rank_lookup[min_message.id];
                //candidate_TS:=RTS
                candidate_seq=min_message.seq_no;
                have_inquired=false;
                break;

            case RELEASE:
                // If (Waiting_Q is not empty)
                printd("%d: waiting_queue size= %d",world_rank,heap_size);
                printHeap();
                //for(int i=0;i<heap_size;++i){
                //    printMessage(waiting_queue[i].msg,world_rank);
                //}
                if(heap_size!=0){
                    //Sort the waiting waiting_queue - no need as now heap is in charge!
                    //qsort(waiting_queue,top,sizeof(message), compare);
                    //Remove(s,rts) from Waiting_Q such that rts is minimum
                    min_message=extractMin();
                    //min_message=waiting_queue[top--];
                    //Send(YES,i) to s
                    send_message.msg=YES;
                    send_message.seq_no=recv_message.seq_no;
                    send_message.id=world_rank;
                    //debugging - changed 4th argument from sender_rank to min_message.id
                    MPI_Send(&send_message,1,mpi_message,min_message.id,0,MPI_COMM_WORLD);
                    printSentMessage(send_message,min_message.id);
                    //candidate:=s.
                    voted_candidate=min_message.id;//local_rank_lookup[min_message.id];
                    //candidate_TS:=RTS
                    candidate_seq=min_message.seq_no;
                    have_inquired=false;
                }
                else{
                    //Have_voted:= false
                    have_voted=false;
                    //Have_inquired:= false
                    have_inquired=false;
                }
                break;
            case INQUIRE:
                //If Critical section is not reached, just forward the msg to EntrySection
                //So send to same world_rank
                if(inCS==0){
                    MPI_Send(&recv_message,1,mpi_message,world_rank,0,MPI_COMM_WORLD);
                    printf("Forwarded the INQUIRE message to EntrySection\n");
                }
                break;

            default:
                printf("%d:WARNING - THIS WARNING IS UNEXPECTED! Inside messageHandlingSection \n",world_rank);
                printf("%d:Got Message with message=%d, seq_no=%d, from_rank=%d at node %d\n",
                        world_rank,recv_message.msg,recv_message.seq_no,recv_message.id,world_rank);
        }
    }
    printf("\n*********THIS SHOULD NOT HAPPEN : Exited from Message Handling Section!***********\n");
    return 0;
}



MPI_Comm *comm_district; //For communication among voting district groups
int main(int argc, char *argv[]) {
    srand(time(NULL));//For random number
    int world_rank, //for storing rank of a node in MPI_COMM_WORLD
        world_size,  //number of nodes in the MPI_COMM_WORLD
        district_size, //size of a district
        //district_rank, //rank in a district
        length,        //for storing length of hostname
        **voting_district, // For storing the voting district arrays.
        seq=0            //Sequence number(Timestamp)
        //color         //For assigning nodes to different voting districts
        ;

    char hostname[MPI_MAX_PROCESSOR_NAME]   //hostname of a node
        ;
    //Initializing MPI
    //MPI_Init(NULL,NULL);
    int provided;
    MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &provided);
    //Create new datatype for message
    int count= 3; //number of blocks
    int array_of_blocklengths[3]={1,1,1}; //number of elements in each block
    MPI_Datatype array_of_types[3]={MPI_INT,MPI_INT, MPI_INT}; //type of elements in each block
    MPI_Datatype mpi_message; //new datatype (handle)
    MPI_Aint array_of_displacements[3];//byte displacement of each block
    array_of_displacements[0] = offsetof(message,msg);
    array_of_displacements[1] = offsetof(message,seq_no);
    array_of_displacements[2] = offsetof(message,id);
    MPI_Type_create_struct(count,
                        array_of_blocklengths,
                         array_of_displacements,
                         array_of_types,
                         &mpi_message);
    MPI_Type_commit(&mpi_message);
    //get communication world size
    MPI_Comm_size(MPI_COMM_WORLD,&world_size);
    //get rank of a node
    MPI_Comm_rank(MPI_COMM_WORLD,&world_rank);
    //get hostname
    MPI_Get_processor_name(hostname, &length);

    printf("%d:Number of nodes : %d\n",world_rank,world_size);
    district_size=world_size-1;
    voting_district=(int **)malloc(sizeof (int *)*world_size +world_size*(sizeof (int **)*(world_size-1)));
    int *data=voting_district+world_size;
    for(size_t i=0;i<world_size;++i)
        voting_district[i]=data+i*(world_size-1);
    length=createVotingDistricts("voting_district.config",world_size,voting_district);

    //Barrier to load the configuration file
    printd("DEBUG : World rank %d : %s\n",world_rank,hostname);
    MPI_Barrier(MPI_COMM_WORLD);

    if(world_rank==0){
    //check vd
        for(int i=0;i<world_size;++i){
            for(int j=0;j<world_size-1;++j)
                printd("%d ",voting_district[i][j]);
            printd("\n");
        }
    }

    int r; //for random number
    #pragma omp parallel shared(mpi_message)
    #pragma omp single
    {
        #pragma omp task
        messageHandlingSection(world_rank,&seq,district_size,mpi_message,voting_district[world_rank]);

            while(1){
                r = rand()%world_size;
                if(r==world_rank){
                    if(!enterCS(world_rank,&seq,district_size,mpi_message,voting_district[world_rank])){
                        criticalSection(world_rank);
                        exitCS(world_rank,district_size,mpi_message,voting_district[world_rank]);
                    }
                }
            }
    }

    MPI_Type_free(&mpi_message);
    MPI_Finalize();
    return 0;
}
