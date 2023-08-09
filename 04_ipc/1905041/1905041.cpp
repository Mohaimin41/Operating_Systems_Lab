#include <iostream>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <random>
#include <time.h>
#include <chrono>
#include <cstdio>

#define NUM_PS 4
#define NUM_BS 2
#define NUM_STAFF 2

#define PRINTING 0             // student is using printing station i.e. in printing time
#define INTERESTED_PRINT 1     // student is in printing station waiting room
#define NOT_INTERESTED_PRINT 2 // student has done printing

#define is_leader(i) ((i + 1) % GRP_SIZE == 0) ? true : false
#define grp_start_for(k) (k / GRP_SIZE) * GRP_SIZE
#define grp_end_for(k) grp_start_for(k) + GRP_SIZE - 1

int NUM_STUDENTS; // total num of students, N
int GRP_SIZE;     // size of a student grp, M
int PRINT_TIME;   // time taken in printing station, w
int BIND_TIME;    // time taken in binding station, x
int RD_WR_TIME;   // time taken to read/write entry book, y

int ENTRY_BOOK; // shared library book
int RDR_COUNT;  // current num of reader on ENTRY_BOOK

char *input_file = (char *)"input.txt";
char *output_file = (char *)"output.txt";

std::chrono::steady_clock::time_point time_start; // starting time to time events

pthread_t *std_threads;                              // threads for students
pthread_t *staff_threads = new pthread_t[NUM_STAFF]; // threads for staffs

int *student_print_state; // dynamic array for student state w.r.t. printing station

sem_t *student_thread_sem;                      // dynamic array for semaphore on student thread
pthread_mutex_t print_state_mutex;              // mutex to lock the print_state array
pthread_mutex_t entry_book_mutex;               // mutex to lock the entry book variable
pthread_mutex_t rdr_cnt_mutex;                  // mutex to lock the RDR_COUNT variable
pthread_mutex_t output_stream_mutex;            // mutex to lock cout
pthread_mutex_t printing_stations_lock[NUM_PS]; // mutex to lock the stations
sem_t binding_station_sem;                      // semaphore on binding station

unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
std::default_random_engine generator2(seed); // random generator for student
std::mt19937 generator;                      // random generator for staff
std::poisson_distribution<int> pd(3.1);      // poisson distribution with mean 3.1

long long int next_arrival_time;  // student sleeps this amount of time before
                                  // arrival in printing station
long long int staff_arrival_time; // staff sleeps this amount of time before read

bool printing_station_empty[NUM_PS] = {true, true, true, true}; // holds state of printing station

/// @brief  similar to take_fork() in dining philosopher, tries to give the
///         student access to designated printing station
/// @param  i: int id of student arriving
void arrive_PS(int);

/// @brief sleeps for length of time given for printing
void do_print();

/// @brief  similar to put_fork() in dining philosopher, releases the printing station,
///         informs students waiting
/// @param  i: int id of student leaving
void leave_PS(int);

/// @brief  informs own group students first by calling get_PS(id) on them, then
///         on other students
/// @param  i: int id of student informing
void inform_students(int);

/// @brief  similar to TEST() in dining philosophers, checks and gives printing
///         station to student with ID if possible
/// @param  self: int id of student to be given printing station if possible
void get_PS(int);

/// @brief  downs BINDING_STATION_SEM to give access to a binding station, prints
///         timing info
/// @param  i: int id of student doing binding
void do_bind(int);

/// @brief  similar to read() in reader writer, does priority read for staffs
/// @param  id: int id of staff reading the entry book
void reader_entry(int);

/// @brief  similar to write() in reader writer, adds 1 to entry book for student
/// @param  id: int id of student writing to the entry book
void writer_entry(int);

/// @brief  initialize the semaphores with 0, except binding_station_sem, it is
///         initialized to the number of stations. The mutex locks are initialized
///         with mutexattr 0
void init_sems()
{
    for (int i = 0; i < NUM_STUDENTS; i++)
        sem_init(student_thread_sem + i, 0, 0);

    pthread_mutex_init(&print_state_mutex, 0);
    pthread_mutex_init(&entry_book_mutex, 0);
    pthread_mutex_init(&rdr_cnt_mutex, 0);
    pthread_mutex_init(&output_stream_mutex, 0);
    for (int i = 0; i < NUM_PS; i++)
        pthread_mutex_init(&printing_stations_lock[i], 0);

    sem_init(&binding_station_sem, 0, NUM_BS);
}

/// @brief  does the work of normal student and if applicable leader
/// @param  arg: void* arg is cast (with precision loss) to int as id of student
/// @return nullptr
void *student_thread_func(void *arg)
{
    next_arrival_time = pd(generator2); // add random delay for arrival

    sleep(next_arrival_time);

    int id = (int)arg;

    arrive_PS(id); // first phase
    do_print();
    leave_PS(id); // dining philosopher end

    if (is_leader(id))
    {
        // join the grp members, as they would handover report to leader and
        // then the grp would finish printing, so the leader waits for them
        for (int i = grp_start_for(id); i < grp_end_for(id); i++)
        {
            pthread_join(std_threads[i], NULL);
        }

        pthread_mutex_lock(&output_stream_mutex);
        std::cout << "Group " << (id + 1) / GRP_SIZE << " has finished printing at time "
                  << std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now() - time_start)
                         .count()
                  << "\n";
        pthread_mutex_unlock(&output_stream_mutex);

        // second phase: using binding station
        do_bind(id);
        // third phase: writing to entry book
        writer_entry(id);
    }
    return nullptr;
}

/// @brief  staff reads the entry_book in random interval for
///         NUM_STUDENTS/GRP_SIZE times. Staff is given priority as reader
/// @param  arg: void* cast to int with lost precision for staff id
/// @return nullptr
void *staff_thread_func(void *arg)
{
    int id = (int)arg;
    int turn = (NUM_STUDENTS / GRP_SIZE) > GRP_SIZE
                   ? NUM_STUDENTS / GRP_SIZE
                   : GRP_SIZE;

    // ensures random gap between two staffs readings
    staff_arrival_time += pd(generator);

    while (turn--)
    {
        sleep(staff_arrival_time);
        // random gap between single staff's readings
        staff_arrival_time += pd(generator);
        reader_entry(id);
    }
    return nullptr;
}

int main()
{
    freopen(input_file, "r", stdin);
    freopen(output_file, "w+", stdout);

    std::cin >> NUM_STUDENTS >> GRP_SIZE >> PRINT_TIME >> BIND_TIME >> RD_WR_TIME;

    std_threads = new pthread_t[NUM_STUDENTS];
    student_print_state = new int[NUM_STUDENTS];
    student_thread_sem = new sem_t[NUM_STUDENTS];

    init_sems();

    time_start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_STUDENTS; i++)
    {
        int res = pthread_create(&std_threads[i], NULL, student_thread_func, (void *)i);
        if (res)
        {
            std::cout << "ERROR: return code from pthread_create is " << res << "\n";
            return 1;
        }
    }
    for (int i = 0; i < NUM_STAFF; i++)
    {
        int res = pthread_create(&staff_threads[i], NULL, staff_thread_func, (void *)i);
        if (res)
        {
            std::cout << "ERROR: return code from pthread_create is " << res << "\n";
            return 1;
        }
    }

    // main() will block and be kept alive to support the threads it created,
    // til they are done
    pthread_exit(NULL);
    return 0;
}

void arrive_PS(int i)
{
    // locking so get_ps() can access and modify(if possible) the print_state array
    pthread_mutex_lock(&print_state_mutex);

    pthread_mutex_lock(&output_stream_mutex);
    std::cout << "Student " << i + 1 << " has arrived in the print station at time "
              << std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - time_start)
                     .count()
              << "\n";
    pthread_mutex_unlock(&output_stream_mutex);

    // change student state for get_PS() call
    student_print_state[i] = INTERESTED_PRINT;

    // this will try to give station if possible and up the thread_sem semaphore
    get_PS(i);
    pthread_mutex_unlock(&print_state_mutex);

    // successful get_PS() would have given a station and upped the semaphore,
    // otherwise thread is blocked here till get_PS() call by any other thread
    // finally ups the semaphore
    sem_wait(&student_thread_sem[i]);

    pthread_mutex_lock(&output_stream_mutex);
    std::cout << "Student " << i + 1 << " has started printing at time "
              << std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - time_start)
                     .count()
              << "\n";
    pthread_mutex_unlock(&output_stream_mutex);
}

void do_print()
{
    sleep(PRINT_TIME);
}

void leave_PS(int i)
{
    pthread_mutex_lock(&print_state_mutex);

    pthread_mutex_lock(&output_stream_mutex);
    std::cout << "Student " << i + 1 << " has finished printing at time "
              << std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - time_start)
                     .count()
              << "\n";
    pthread_mutex_unlock(&output_stream_mutex);

    // ensure student is out of the waiting room of station
    student_print_state[i] = NOT_INTERESTED_PRINT;
    pthread_mutex_unlock(&print_state_mutex);

    // inform as in call get_PS() on them, thus telling them the station is free
    // and giving access to one of them, if they were interested in it
    inform_students(i);
}

void get_PS(int i)
{
    // only give station if this student is in waiting room AND their designated
    // station is empty, changing the states and upping the students thread_sem
    if (student_print_state[i] == INTERESTED_PRINT && printing_station_empty[i % NUM_PS])
    {
        pthread_mutex_lock(&printing_stations_lock[i % NUM_PS]);
        printing_station_empty[i % NUM_PS] = false;
        pthread_mutex_unlock(&printing_stations_lock[i % NUM_PS]);
 
        student_print_state[i] = PRINTING;
        sem_post(&student_thread_sem[i]);
    }
}

void inform_students(int self)
{
    printing_station_empty[self % NUM_PS] = true;

    // get_PS changes the print_state_array so need to lock before calling it
    pthread_mutex_lock(&print_state_mutex);

    // call on group mates first
    for (int i = grp_start_for(self); i <= grp_end_for(self); i++)
    {
        if (i % NUM_PS == self % NUM_PS)
        // if they are waiting in same printing station
        {
            get_PS(i);
        }
    }

    // call on others designated to this same station then
    // with 0 based student and station ids: (for 4: 0, 4, 8...)
    for (int i = self % NUM_PS; i < NUM_STUDENTS; i += GRP_SIZE)
    {
        get_PS(i);
    }
    pthread_mutex_unlock(&print_state_mutex);
}

void do_bind(int id)
{
    // down semaphore, if station is available it would not block
    sem_wait(&binding_station_sem);

    pthread_mutex_lock(&output_stream_mutex);
    std::cout << "Group " << (id + 1) / GRP_SIZE << " has started binding at time "
              << std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - time_start)
                     .count()
              << "\n";
    pthread_mutex_unlock(&output_stream_mutex);

    sleep(BIND_TIME);

    pthread_mutex_lock(&output_stream_mutex);
    std::cout << "Group " << (id + 1) / GRP_SIZE << " has finished binding at time "
              << std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - time_start)
                     .count()
              << "\n";
    pthread_mutex_unlock(&output_stream_mutex);
    // up the semaphore at end of binding, releasing the station
    sem_post(&binding_station_sem);
}

void reader_entry(int id)
{
    pthread_mutex_lock(&rdr_cnt_mutex);
    RDR_COUNT += 1; // add to reader count
    if (RDR_COUNT == 1)
    // reader(s) have wanted access again, so lock entry, giving readers priority
    {
        pthread_mutex_lock(&entry_book_mutex);
    }
    pthread_mutex_unlock(&rdr_cnt_mutex);

    // accessing the entry_book
    pthread_mutex_lock(&output_stream_mutex);
    std::cout << "Staff " << (id + 1) << " has started reading the entry book at time "
              << std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - time_start)
                     .count()
              << ". No. of submission " << ENTRY_BOOK << "\n";
    pthread_mutex_unlock(&output_stream_mutex);

    sleep(RD_WR_TIME);

    pthread_mutex_lock(&output_stream_mutex);
    std::cout << "Staff " << (id + 1) << " has finished reading the entry book at time "
              << std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - time_start)
                     .count()
              << ". No. of submission " << ENTRY_BOOK << "\n";
    pthread_mutex_unlock(&output_stream_mutex);

    pthread_mutex_lock(&rdr_cnt_mutex);
    RDR_COUNT -= 1;
    if (RDR_COUNT == 0) // if no more reader on entry_book, release for writer(s)
    {
        pthread_mutex_unlock(&entry_book_mutex);
    }
    pthread_mutex_unlock(&rdr_cnt_mutex);
}

void writer_entry(int id)
{
    pthread_mutex_lock(&entry_book_mutex);
    ENTRY_BOOK += 1; // writers add their submission to book

    pthread_mutex_lock(&output_stream_mutex);
    std::cout << "Group " << (id + 1) / GRP_SIZE << " has submitted the report at time "
              << std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - time_start)
                     .count()
              << "\n";
    pthread_mutex_unlock(&output_stream_mutex);

    sleep(RD_WR_TIME);
    pthread_mutex_unlock(&entry_book_mutex);
}