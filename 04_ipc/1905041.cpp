#include <iostream>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <random>
#include <time.h>

#define PRINTING 0
#define INT_PRINT 1
#define NOT_INT_PRINT 2
#define BINDING 3
#define INT_BIND 4
#define NOT_INT_BIND 5

#define is_leader(i) ((i + 1) % GRP_SIZE == 0) ? true : false
#define grp_start_for(k) ((k / GRP_SIZE)) * GRP_SIZE
#define grp_end_for(k) grp_start_for(k) + GRP_SIZE - 1

int CURR_TIME;
int NUM_STUDENTS;
int GRP_SIZE;
int PRINT_TIME;
int BIND_TIME;
int RD_WR_TIME;

int SUB_COUNT; // shared library book
int RDR_COUNT; // current num of reader on SUB_COUNT

clock_t time_start;

pthread_t *std_threads;
pthread_t *staff_threads = new pthread_t[2];

int *student_print_state;
int *leader_bind_state;

sem_t *student_thread_sem;
pthread_mutex_t print_state_mutex;
sem_t *leader_thread_sem;
pthread_mutex_t bind_state_mutex;
pthread_mutex_t sub_book_mutex;
pthread_mutex_t rdr_cnt_mutex;
// sem_t PS_sems[4];

static std::random_device r_dev;
static std::mt19937_64 generator(r_dev());
std::poisson_distribution<int> pd(3);

int next_arrival_time;

void arrive_PS(int);
void do_print(int);
void leave_PS(int);
void inform_students(int);
void get_PS(int);
void arrive_BS(int, int *);
void do_bind(int);
void leave_BS(int, int *);
int get_BS(int);
void inform_leaders(int, int *);

struct printing_station
{
    bool empty;
    printing_station() { empty = true; }
};

struct binding_station
{
    bool empty;
    binding_station() { empty = true; }
};

struct printing_station PS[4];
struct binding_station BS[2];

void init_sems()
{
    for (int i = 0; i < NUM_STUDENTS; i++)
        sem_init(student_thread_sem + i, 0, 0);
    for (int i = 0; i < NUM_STUDENTS / GRP_SIZE; i++)
        sem_init(leader_thread_sem + i, 0, 0);

    pthread_mutex_init(&print_state_mutex, 0);
    pthread_mutex_init(&bind_state_mutex, 0);
    pthread_mutex_init(&sub_book_mutex, 0);
    pthread_mutex_init(&rdr_cnt_mutex, 0);
}

void *student_thread_func(void *arg)
{
    int bs_num_for_leader = -1;
    next_arrival_time += pd(generator);
    // next_arrival_time += 2;
    sleep(next_arrival_time);
    int id = (int)arg;
    arrive_PS(id);
    do_print(id);
    leave_PS(id);
    if (is_leader(id))
    {
        for (int i = grp_start_for(id); i < grp_end_for(id); i++)
        {
            pthread_join(std_threads[i], NULL);
        }
        std::cout << "Group " << (id + 1) / GRP_SIZE << " has finished printing at time "
                  << (int)(clock() - time_start) / 100 << "\n";
        arrive_BS(id, &bs_num_for_leader);
        do_bind(id);
        leave_BS(id, &bs_num_for_leader);
    }
    return nullptr;
}

void *staff_thread_func(void *arg)
{
    return nullptr;
}

int main()
{
    std::cin >> NUM_STUDENTS >> GRP_SIZE >> PRINT_TIME >> BIND_TIME >> RD_WR_TIME;

    std_threads = new pthread_t[NUM_STUDENTS];
    leader_bind_state = new int[NUM_STUDENTS / GRP_SIZE];
    student_print_state = new int[NUM_STUDENTS];
    student_thread_sem = new sem_t[NUM_STUDENTS];
    leader_thread_sem = new sem_t[NUM_STUDENTS / GRP_SIZE];

    init_sems();
    time_start = clock();

    for (int i = 0; i < NUM_STUDENTS; i++)
    {
        int res = pthread_create(&std_threads[i], NULL, student_thread_func, (void *)i);
        if (res)
        {
            std::cout << "ERROR: return code from pthread_create is " << res << "\n";
            return 1;
        }
    }
    pthread_exit(NULL);
    return 0;
}

void arrive_PS(int i)
{
    pthread_mutex_lock(&print_state_mutex);
    std::cout << "Student " << i + 1 << " has arrived in the print station at time "
              << (int)(clock() - time_start) / 100 << "\n";

    student_print_state[i] = INT_PRINT;
    get_PS(i);
    pthread_mutex_unlock(&print_state_mutex);
    sem_wait(&student_thread_sem[i]);
}

void do_print(int i)
{
    sleep(PRINT_TIME);
}

void leave_PS(int i)
{
    pthread_mutex_lock(&print_state_mutex);
    std::cout << "Student " << i + 1 << " has finished printing at time "
              << (int)(clock() - time_start) / 100 << "\n";

    student_print_state[i] = NOT_INT_PRINT;
    pthread_mutex_unlock(&print_state_mutex);
    inform_students(i);
}

void get_PS(int i)
{
    if (student_print_state[i] == INT_PRINT && PS[i % 4].empty)
    {
        PS[i % 4].empty = false;
        student_print_state[i] = PRINTING;
        sem_post(&student_thread_sem[i]);
    }
}
void inform_students(int self)
{
    PS[self % 4].empty = true;

    for (int i = grp_start_for(self); i <= grp_end_for(self); i++)
    {
        get_PS(i);
    }

    for (int i = self % 4; i < NUM_STUDENTS; i += GRP_SIZE)
    {
        get_PS(i);
    }
}

void arrive_BS(int id, int *bs_num)
{
    pthread_mutex_lock(&bind_state_mutex);
    std::cout << "Group " << (id + 1) / GRP_SIZE << " has started binding at time "
              << (int)(clock() - time_start) / 100 << "\n";
    leader_bind_state[id / GRP_SIZE] = INT_BIND;
    *bs_num = get_BS(id);
    pthread_mutex_unlock(&bind_state_mutex);
    sem_wait(&leader_thread_sem[id / GRP_SIZE]);
}

void do_bind(int id) { sleep(BIND_TIME); }
void leave_BS(int id, int *bs_num)
{
    pthread_mutex_lock(&bind_state_mutex);
    std::cout << "Group " << (id + 1) / GRP_SIZE << " finished binding at time "
              << (int)(clock() - time_start) / 100 << "\n";
    leader_bind_state[id / GRP_SIZE] = NOT_INT_BIND;
    pthread_mutex_unlock(&bind_state_mutex);
    inform_leaders(id, bs_num);
}
int get_BS(int id)
{
    int bs_num = -1;
    if (leader_bind_state[id / GRP_SIZE] == INT_BIND && (BS[0].empty || BS[1].empty))
    {
        leader_bind_state[id / GRP_SIZE] = BINDING;
        if (BS[0].empty)
        {
            BS[0].empty = false;
            bs_num = 0;
        }
        else if (BS[1].empty)
        {
            BS[1].empty = false;
            bs_num = 1;
        }
        sem_post(&leader_thread_sem[id / GRP_SIZE]);
    }
    return bs_num;
}
void inform_leaders(int id, int *bs_num)
{
    BS[*bs_num].empty = true;
    for (int i = GRP_SIZE - 1; i < NUM_STUDENTS; i += GRP_SIZE)
    {
        get_BS(i);
    }
}