// Copyright @lucabotez

#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACKER_RANK 0
#define MAX_FILES 100
#define MAX_FILENAME 15
#define HASH_SIZE 33 // 32 + \0
#define MAX_CHUNKS 100
#define MAX_CLIENTS 100
#define BUF_SIZE 15 // buffer used for communication between the tasks

// tags used in different stages of communication to avoid receiving
// unwanted messages
typedef enum {
    INIT_TAG = 1,
    INIT_DATA_TAG,
    TYPE_TAG,
    DATA_TAG,
    DATA_UPDATE_TAG,
    DOWNLOAD_TAG,
    DOWNLOAD_INFO_TAG,
    DOWNLOAD_RESPONSE_TAG
} MPI_Tags;

// structure used to hold a file's information for a client task
typedef struct {
    // the name of the file
    char filename[MAX_FILENAME];
    // a vector of strings that holds all the chunk hashes of the file
    char hashes[MAX_CHUNKS][HASH_SIZE];
    // the number of chunks ( == number of hashes)
    int number_of_chunks;
    // as we only work with the hashes, we will simulate the ownership of
    // a chunk by marking its position with 0 or 1
    // 0 => we do not have the chunk
    // 1 => we have the chunk
    int owned_chunks[MAX_CHUNKS];
} file;

// structure used to hold a file's information for the tracker
// the owned_chunks array will be initialised with 0, as the 
// tracker only holds the hashes, not the chunks themselves
typedef struct {
    file file;
    // the array in which all the clients that own at least a chunk of the file
    // are stored
    int swarm[MAX_CLIENTS];
} tracker_file;

// current number of files in the file / file info collections
int number_of_files = 0;

// the file collection of a client
file owned_files[MAX_FILES];

// the file info collection of the tracker
tracker_file tracker_file_info[MAX_FILES];

// the number of desired files and their filenames
// of a client
int number_of_desired_files;
char desired_files[MAX_FILES][MAX_FILENAME];

// mutex used in download/upload threads
pthread_mutex_t file_mutex;

// function used to find a specific's file index in the
// file collection (by the filename)
// rank == 0 => the search is conducted in the tracker's collection
// rank > 0  => the search is conducted in the client's collection
int find_index(int rank, char filename[MAX_FILENAME]) {
    int index = 0;

    if (rank == 0)
        for (index = 0; index < number_of_files; index++)
            if (!strcmp(tracker_file_info[index].file.filename, filename))
                    break;

    if (rank > 0)
        for (index = 0; index < number_of_files; index++)
            if (!strcmp(owned_files[index].filename, filename))
                break;

    // it returns the found index
    return index;
}

// function used to print a received file
void print_file(file file, int rank) {
    char output_file[MAX_FILENAME] = "client";
    char buf[BUF_SIZE];

    // converting the rank to a string
    sprintf(buf, "%d", rank); 

    strcat(output_file, buf);
    strcat(output_file, "_");
    strcat(output_file, file.filename);

    FILE *fout;
    fout = fopen(output_file, "w");

    for (int j = 0; j < file.number_of_chunks; j++)
        fprintf(fout, "%s\n", file.hashes[j]);

    fclose(fout);
}

// function used to add a new client to the swarm of a file, only if
// the client is not already in the swarm
void swarm_update(int found_index, int curr_client) {
    int swarm_index = 0;
    int already_in_swarm = 0; // we don't add it again if it is in the swarm

    while (tracker_file_info[found_index].swarm[swarm_index]) {
        if (tracker_file_info[found_index].swarm[swarm_index] == curr_client) {
            already_in_swarm = 1;
            break;
        }

        swarm_index++;
    }

    if (!already_in_swarm)
        tracker_file_info[found_index].swarm[swarm_index] = curr_client;
}

// function called by the clients used to receive all the input and to send
// their data to the tracker
void client_init(int rank) {
    // getting the filename
    char input_file[MAX_FILENAME] = "in";
    char buf[BUF_SIZE];
    sprintf(buf, "%d", rank);

    strcat(input_file, buf);
    strcat(input_file, ".txt");

    FILE *fin;
    fin = fopen(input_file, "r");

    int client_files;
    fscanf(fin, "%d", &client_files);

    // as we read information we also send it to the tracker
    strcpy(buf, "INIT"); // notifying the tracker before sending the data
    MPI_Send(buf, BUF_SIZE, MPI_CHAR, TRACKER_RANK, INIT_TAG,
             MPI_COMM_WORLD);
    MPI_Send(&client_files, 1, MPI_INT, TRACKER_RANK, INIT_DATA_TAG,
             MPI_COMM_WORLD);

    // reading the filenames, number of chunks, chunk hashes and
    // sending them to the tracker
    for (int i = 0; i < client_files; i++) {
        file file = {0};
        fscanf(fin, "%s %d", file.filename, &file.number_of_chunks);

        MPI_Send(file.filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK,
                 INIT_DATA_TAG, MPI_COMM_WORLD);
        MPI_Send(&file.number_of_chunks, 1, MPI_INT, TRACKER_RANK, INIT_DATA_TAG,
                 MPI_COMM_WORLD);

        for (int j = 0; j < file.number_of_chunks; j++) {
            char hash[HASH_SIZE];
            fscanf(fin, "%s", hash);
            MPI_Send(hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, INIT_DATA_TAG,
                     MPI_COMM_WORLD);

            strcpy(file.hashes[j], hash);
        }

        // marking the ownership of all the chunks
        for (int j = 0; j < file.number_of_chunks; j++) {
            file.owned_chunks[j] = 1;   
        }

        // adding the file to the collection and increasing the
        // number of files
        owned_files[number_of_files++] = file;
    }

    // reading the desired files
    fscanf(fin, "%d", &number_of_desired_files);
    for (int i = 0; i < number_of_desired_files; i++)
        fscanf(fin, "%s", desired_files[i]);

    fclose(fin);
}

// function called by the tracker used to receive information of client owned
// files
void tracker_init(int numtasks, int rank) {
    char buf[BUF_SIZE];

    // the tracker waits for each client to send their information
    // regarding the number of owned files and chunk hashes
    for (int i = 0; i < numtasks - 1; i++) {
        // we need to use MPI_Status to extract the source, so that we can
        // receive the right information in the intended order
        MPI_Status status;

        // the tracker is ready to receive the notification from any client
        MPI_Recv(buf, BUF_SIZE, MPI_CHAR, MPI_ANY_SOURCE, INIT_TAG,
                 MPI_COMM_WORLD, &status);
        int curr_client = status.MPI_SOURCE; // the source
        
        if (!strcmp(buf, "INIT")) {
            // we receive the number of owned files first
            int client_files;
            MPI_Recv(&client_files, 1, MPI_INT, curr_client, INIT_DATA_TAG,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // for each file the tracker stores the filename, number of chunks,
            // hashes, and it creates the swarm
            for (int j = 0; j < client_files; j++) {
                char filename[MAX_FILENAME];
                MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, curr_client,
                         INIT_DATA_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                // searching if the tracker already has info about the file,
                // if not it adds it to the collection
                int found_index = find_index(rank, filename);
                if (found_index == number_of_files) {
                    tracker_file tracker_file = {0};
                    strcpy(tracker_file.file.filename, filename);

                    MPI_Recv(&tracker_file.file.number_of_chunks, 1, MPI_INT,
                             curr_client, INIT_DATA_TAG, MPI_COMM_WORLD,
                             MPI_STATUS_IGNORE);
                    for (int k = 0; k < tracker_file.file.number_of_chunks; k++) {
                        char hash[HASH_SIZE];
                        MPI_Recv(hash, HASH_SIZE, MPI_CHAR, curr_client,
                                 INIT_DATA_TAG, MPI_COMM_WORLD,
                                 MPI_STATUS_IGNORE);

                        strcpy(tracker_file.file.hashes[k], hash);
                    }

                    // creating the swarm, adding the sender as the only owner
                    // of the file for now
                    tracker_file.swarm[0] = curr_client;

                    // adding the file to the collection
                    tracker_file_info[number_of_files++] = tracker_file;
                // the tracker only modifies the file swarm, marking another owner
                // of the file
                } else {
                    // the tracker still receives the number of chunks and hashes,
                    // but it ignores them
                    int iter;
                    char temp_buf[HASH_SIZE];
                    MPI_Recv(&iter, 1, MPI_INT, curr_client, INIT_DATA_TAG,
                             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    for (int i = 0; i < iter; i++)
                        MPI_Recv(temp_buf, HASH_SIZE, MPI_CHAR, curr_client,
                                 INIT_DATA_TAG, MPI_COMM_WORLD,
                                 MPI_STATUS_IGNORE);

                    // modifying the swarm
                    swarm_update(found_index, curr_client);
                }
            }
        }
    }
}

// function used to download a desired file of a client
void receive_file(file file, int swarm[MAX_CLIENTS],
                  int file_index, int rank) {
    // the position in the swarm array
    int swarm_index = 0;

    // the number of downloaded chunks, used to refresh the swarm
    // after 10 successful downloads
    int downloaded_chunks = 0; 

    char buf[BUF_SIZE];

    // in order for a balanced downloading of chunks, each chunk will be
    // downloaded from a different seed/peer (if possible), if the swarm
    // array reaches its end it will start circularly again
    for (int j = 0; j < file.number_of_chunks; j++) {
        // if the chunk is already owned it skips to the next
        if (file.owned_chunks[j])
            continue;

        // if the swarm member is the client itself it skips to the next
        if (swarm[swarm_index] == rank) {
            swarm_index++;
        }

        // if the end of the swarm list is reached it restarts from the beginning
        if (!swarm[swarm_index]) {
            swarm_index = 0;
        }

        // current seed/peer
        int curr_uploader = swarm[swarm_index];

        // sending the necessary information (filename, chunk hash)
        MPI_Send(file.filename, MAX_FILENAME, MPI_CHAR, curr_uploader,
                    DOWNLOAD_TAG, MPI_COMM_WORLD);
        MPI_Send(file.hashes[j], HASH_SIZE, MPI_CHAR, curr_uploader,
                    DOWNLOAD_INFO_TAG, MPI_COMM_WORLD);

        // as we simulate the transfer of chunks, we will only receive "OK" / "NOT_OK"
        // if the seed/peer has or does not have the chunk
        MPI_Recv(buf, BUF_SIZE, MPI_CHAR, curr_uploader,
                    DOWNLOAD_RESPONSE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // going to the next swarm member in the following iteration
        swarm_index++;

        // if the seed/peer did not have the required chunk go to the next in
        // the swarm
        if (strcmp(buf, "OK")) {
            j--;
            continue;
        }

        // updating the owned files collection, marking the possesion of a new
        // chunk of the file
        file.owned_chunks[j] = 1;
        pthread_mutex_lock(&file_mutex);
        owned_files[file_index] = file;
        pthread_mutex_unlock(&file_mutex);

        // counting the successful downloads, for each tenth download the tracker
        // is notified to send the updated swarm for the desired file
        downloaded_chunks++;
        if (downloaded_chunks == 10) {
            strcpy(buf, "FILE_UPDATE");
            MPI_Send(buf, BUF_SIZE, MPI_CHAR, TRACKER_RANK, TYPE_TAG,
                        MPI_COMM_WORLD);
            MPI_Send(file.filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK,
                        DATA_UPDATE_TAG, MPI_COMM_WORLD);
            MPI_Recv(swarm, MAX_CLIENTS, MPI_INT, TRACKER_RANK,
                        DATA_UPDATE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            downloaded_chunks = 0; // the counter is reset
        }
    }
}

// function called by the download thread, used to download all the needed
// files of a client
void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;

    char buf[BUF_SIZE];
    for (int i = 0; i < number_of_desired_files; i++) {
        // notifying the tracker of its intention
        strcpy(buf, "FILE_REQUEST");
        MPI_Send(buf, BUF_SIZE, MPI_CHAR, TRACKER_RANK,
                 TYPE_TAG, MPI_COMM_WORLD);

        // sending the desired filename
        MPI_Send(desired_files[i], MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 
                 DATA_TAG, MPI_COMM_WORLD);

        // creating the file, initializing everything with 0 (marking
        // that for now no chunk is owned)
        file file = {0};
        strcpy(file.filename, desired_files[i]);

        // receiving the required information about the file (number of chunks,
        // chunk hashes)
        MPI_Recv(&file.number_of_chunks, 1, MPI_INT, TRACKER_RANK, DATA_TAG,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        for (int i = 0; i < file.number_of_chunks; i++)
            MPI_Recv(file.hashes[i], HASH_SIZE, MPI_CHAR, TRACKER_RANK,
                     DATA_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        // receiving the file swarm array
        int swarm[MAX_CLIENTS];
        MPI_Recv(swarm, MAX_CLIENTS, MPI_INT, TRACKER_RANK, DATA_TAG,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // having a locally stored copy of the index of the current file
        int file_index; 

        // adding the incomplete file to the collection, soon to be fully
        // downloaded
        pthread_mutex_lock(&file_mutex);
        file_index = number_of_files;
        owned_files[number_of_files++] = file;
        pthread_mutex_unlock(&file_mutex);

        // downloading the file from the swarm members
        receive_file(file, swarm, file_index, rank);

        // printing the file, now fully completed
        print_file(file, rank);
    }

    // all desired files are downloaded, the tracker is notified
    strcpy(buf, "DONE");
    MPI_Send(buf, BUF_SIZE, MPI_CHAR, TRACKER_RANK, TYPE_TAG, MPI_COMM_WORLD);

    // waiting for confirmation to shut everything down
    MPI_Bcast(buf, BUF_SIZE, MPI_CHAR, TRACKER_RANK, MPI_COMM_WORLD);

    // closing the upload thread too
    MPI_Send(buf, BUF_SIZE, MPI_CHAR, rank, DOWNLOAD_TAG, MPI_COMM_WORLD);
    return NULL;
}

// function called by the upload thread, used to upload files to other clients
void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;

    while (1) {
        // the client received a download request for a hash (filename and hash)
        char requested_filename[MAX_FILENAME], requested_hash[HASH_SIZE];
        char buf[BUF_SIZE];
        MPI_Status status;

        // checking if it really is a filename or a SHUTDOWN notification from
        // the download thread of itself
        MPI_Recv(buf, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE,
                 DOWNLOAD_TAG, MPI_COMM_WORLD, &status);
        if (!strcmp(buf, "SHUTDOWN"))
            return NULL;

        strcpy(requested_filename, buf);
        int curr_client = status.MPI_SOURCE; // extracting the client
        MPI_Recv(requested_hash, HASH_SIZE, MPI_CHAR, curr_client,
                 DOWNLOAD_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // searching for the index of the file that contains the requested chunk
        // a mutex lock/unlock is not used here as the files from the collection
        // are never erased and the order stays the same
        int found_index = find_index(rank, requested_filename);

        // extracting the file from the collection
        // this time a mutex lock/unlock is necessary, as chunks may be
        // donwloading concurrently
        file found_file;
        pthread_mutex_lock(&file_mutex);
        found_file = owned_files[found_index];
        pthread_mutex_unlock(&file_mutex);

        // checking if the chunk is owned and sending a corresponding message
        int send_flag = 1;
        for (int hash_index = 0; hash_index < found_file.number_of_chunks;
             hash_index++) {
            if (!strcmp(found_file.hashes[hash_index], requested_hash) && 
                found_file.owned_chunks[hash_index]) {
                send_flag = 0;

                strcpy(buf, "OK");
                MPI_Send(buf, BUF_SIZE, MPI_CHAR, curr_client,
                         DOWNLOAD_RESPONSE_TAG, MPI_COMM_WORLD);

                break;
            }
        }

        if (send_flag == 1) {
            strcpy(buf, "NOT_OK");
            MPI_Send(buf, BUF_SIZE, MPI_CHAR, curr_client,
                     DOWNLOAD_RESPONSE_TAG, MPI_COMM_WORLD);
        }
    }
}

// function executed by the tracker
void tracker(int numtasks, int rank) {
    // the number of clients that have completed all their downloads is kept
    // in ready_to_close; when it reaches (numtasks - 1) every client is
    // ready for shutdown
    int ready_to_close = 0;
    char buf[BUF_SIZE];

    // receiving data regarding the files of each client
    tracker_init(numtasks, rank);

    // signal every client that the tracker is ready after receiving
    // data from them
    strcpy(buf, "TRACKER_READY");
    MPI_Bcast(buf, BUF_SIZE, MPI_CHAR, TRACKER_RANK, MPI_COMM_WORLD);

    // start the execution
    while (1) {
        MPI_Status status;
        MPI_Recv(buf, BUF_SIZE, MPI_CHAR, MPI_ANY_SOURCE, TYPE_TAG,
                 MPI_COMM_WORLD, &status);

        int curr_client = status.MPI_SOURCE; // extracting the source
        if (!strcmp(buf, "FILE_REQUEST")) { // REQUEST CASE
            char filename[MAX_FILENAME];
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, curr_client, DATA_TAG,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // finding the file in the collection and sending 
            // the required info (number of chunks, chunk hashes)
            int found_index = find_index(rank, filename);
            tracker_file found_file = tracker_file_info[found_index];

            MPI_Send(&found_file.file.number_of_chunks, 1, MPI_INT, curr_client,
                     DATA_TAG, MPI_COMM_WORLD);

            for (int i = 0; i < found_file.file.number_of_chunks; i++) {
                MPI_Send(found_file.file.hashes[i], HASH_SIZE, MPI_CHAR,
                         curr_client, DATA_TAG, MPI_COMM_WORLD);
            }

            // sending the swarm
            MPI_Send(found_file.swarm, MAX_CLIENTS, MPI_INT, curr_client,
                     DATA_TAG, MPI_COMM_WORLD);

            // modifying the swarm by adding the client that requested
            // the information
            swarm_update(found_index, curr_client);
        } else if (!strcmp(buf, "FILE_UPDATE")) { // SWARM REQUEST CASE
            char filename[MAX_FILENAME];
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, curr_client,
                     DATA_UPDATE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // finding the file in the collection
            int found_index = find_index(rank, filename);

            // sending the swarm
            MPI_Send(tracker_file_info[found_index].swarm,
             MAX_CLIENTS, MPI_INT, curr_client, DATA_UPDATE_TAG, MPI_COMM_WORLD);
        } else if (!strcmp(buf, "DONE")) { //SHUTDOWN CASE
            ready_to_close++;
            if (ready_to_close == numtasks - 1) {
                break;
            }
        }
    }

    // notifying every client to finish execution
    strcpy(buf, "SHUTDOWN");
    MPI_Bcast(buf, BUF_SIZE, MPI_CHAR, TRACKER_RANK, MPI_COMM_WORLD);
}

// function executed by the clients (seeds, peers, leechers)
void client(int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    // reading data from the file and sending information to the tracker
    client_init(rank);

    // waiting for the tracker confirmation to start the execution
    char buf[BUF_SIZE];
    MPI_Bcast(buf, BUF_SIZE, MPI_CHAR, TRACKER_RANK, MPI_COMM_WORLD);

    // the mutex used in the download/upload threads
    r = pthread_mutex_init(&file_mutex, NULL);
    if (r) {
        printf("Error while creating the mutex.\n");
        exit(-1);
    }

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Error while creating the download thread.\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Error while creating the upload thread.\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Error while waiting for the download thread.\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Error while waiting for the upload thread.\n");
        exit(-1);
    }

    pthread_mutex_destroy(&file_mutex);
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI does not have support for multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        client(rank);
    }

    MPI_Finalize();
}
