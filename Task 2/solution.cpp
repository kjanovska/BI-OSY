#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <semaphore.h>
#include "common.h"
using namespace std;
#endif /* __PROGTEST__ */

class MyStack{
private:
    pthread_mutex_t mtx;
    uint32_t  * free_pages;
    uint32_t size;
public:
    MyStack();
    ~MyStack();
    uint32_t pop();
    void push(uint32_t page_number);
    uint32_t getSize();
};

void MyStack::push(uint32_t page_number) {
    pthread_mutex_lock(&this->mtx);
    uint32_t * bigger;
    bigger = new uint32_t [this -> size + 1];
    for ( uint32_t i = 0; i < this -> size; i++ ){
        *(bigger + i) = this ->free_pages[i];
    }
    delete[] this->free_pages;
    this->free_pages = bigger;
    this -> size++;
    this -> free_pages[this -> size - 1] = page_number;
    pthread_mutex_unlock(&this->mtx);
}

uint32_t MyStack::pop() {
    pthread_mutex_lock(&this->mtx);
    uint32_t topPage = this ->free_pages[this->size - 1];
    uint32_t * smaller;
    smaller = new uint32_t [this -> size - 1];
    for ( uint32_t i = 0; i < this -> size - 1; i++ ){
        smaller[i] = this ->free_pages[i];
    }
    delete[] this->free_pages;
    this->free_pages = smaller;
    this -> size--;
    pthread_mutex_unlock(&this->mtx);
    return topPage;
}

MyStack::MyStack() {
    this -> size = 1;
    this -> free_pages = new uint32_t [size];
    pthread_mutex_init(&this->mtx, NULL);
}

MyStack::~MyStack() {
    delete[]this->free_pages;
}

uint32_t MyStack::getSize() {
    pthread_mutex_lock(&this->mtx);
    uint32_t tmpSize = this->size;
    pthread_mutex_unlock(&this->mtx);
    return tmpSize;
}

struct ForNewProcess{
    void (* entryPoint)(CCPU *, void*);
    CCPU * newCCPUKiddo = nullptr;
    void * processArg = nullptr;
};

MyStack * stack;
pthread_mutex_t rpMtx; //mutex for running_processes access
int running_processes = 0;
pthread_cond_t condVar;

void * helper(void * args){
    auto * a = (ForNewProcess *)args;
    a->entryPoint(a->newCCPUKiddo,a->processArg);
    delete a;

    pthread_mutex_lock(&rpMtx);
    running_processes--;
    pthread_cond_signal(&condVar);
    pthread_mutex_unlock(&rpMtx);

    return args;
}

class CCPUKiddo : public CCPU
{
private:
    uint32_t currentMemLimit;
public:
    CCPUKiddo(uint8_t *memStart, uint32_t pageTableRoot);
    ~CCPUKiddo();
    virtual uint32_t         GetMemLimit                   (void ) const;
    virtual bool             SetMemLimit                   ( uint32_t          pages );
    virtual bool             NewProcess                    ( void            * processArg,
                                                             void           (* entryPoint) ( CCPU *, void * ),
                                                             bool              copyMem );
    bool                     MakeBigger                    ( uint32_t          pages );
    bool                     MakeSmaller                   ( uint32_t          pages );
    void printTable();
    uint32_t * rootPageTable; // pointer to root beginning
};

uint32_t CCPUKiddo::GetMemLimit(void) const {
    return this -> currentMemLimit;
}

bool CCPUKiddo::SetMemLimit(uint32_t pages) {
    if ( pages > this->currentMemLimit) return this->MakeBigger(pages);
    else if ( pages < this->currentMemLimit ) return this->MakeSmaller(pages);
    else return true; // ==
}

bool CCPUKiddo::NewProcess(void *processArg, void (*entryPoint)(CCPU *, void *), bool copyMem) {
    pthread_mutex_lock(&rpMtx);
    if ( running_processes == 64 )
    {
        pthread_mutex_unlock(&rpMtx);
        return false;
    }
    running_processes++;
    pthread_mutex_unlock(&rpMtx);
    uint32_t rootPage = stack->pop();
    CCPUKiddo  * newCpu = new CCPUKiddo(this->m_MemStart, rootPage*4096);

    if ( copyMem ){
        newCpu->SetMemLimit(this->currentMemLimit);
        for ( uint32_t i = 0; i < 1024; i++ ){
            if ( this->rootPageTable[i] != 0 ){
                uint32_t l2FROM_number = this->rootPageTable[i] >> OFFSET_BITS;
                uint32_t * l2FROM_table = (uint32_t *)(this -> m_MemStart + (l2FROM_number * PAGE_SIZE));

                uint32_t l2TO_number = newCpu->rootPageTable[i] >> OFFSET_BITS;
                uint32_t * l2TO_table = (uint32_t *)(this -> m_MemStart + (l2TO_number * PAGE_SIZE));
                for ( uint32_t j = 0; j < 1024; j++ ){
                    if ( l2FROM_table[j] != 0 ) {
                        uint32_t dataPageNumber = l2FROM_table[j] >> OFFSET_BITS;
                        uint32_t * dataPageTable = (uint32_t *) (this->m_MemStart + (dataPageNumber * PAGE_SIZE));
                        uint32_t newDPNumber = l2TO_table[j] >> OFFSET_BITS;
                        uint32_t * newDPTable = (uint32_t *) (this->m_MemStart + (newDPNumber * PAGE_SIZE));
                        memcpy(newDPTable, dataPageTable, 1024*4);
                    }
                }
            }
        }
    }

    ForNewProcess * a = new ForNewProcess();
    a->processArg = processArg;
    a->entryPoint = entryPoint;
    a->newCCPUKiddo = newCpu;
    if ( a->newCCPUKiddo == nullptr) printf("AAAAAAAAAAAAA\n");
    // tvorim DETACHED vlakno
    pthread_t newThread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&newThread, &attr, &helper, a);
    pthread_attr_destroy(&attr);
    return true;
}

CCPUKiddo::CCPUKiddo(uint8_t *memStart, uint32_t pageTableRoot) : CCPU(memStart, pageTableRoot) {
    this -> currentMemLimit = 0;
    // init rootPageTable to 1024 zeros and set up adress to m_pageTableRoot
    this ->rootPageTable = (uint32_t *)(memStart + (pageTableRoot & ADDR_MASK)); // start rootPageTable
    for ( uint32_t i = 0; i < PAGE_DIR_ENTRIES; i++ ){
        this->rootPageTable[i] = 0;
    }
    uint32_t newL2Page = stack->pop(); // number
    // adress of a page
    this -> rootPageTable[0] = (newL2Page << OFFSET_BITS) | BIT_USER | BIT_WRITE | BIT_PRESENT;
    uint32_t * l2pageTable = (uint32_t *)(this -> m_MemStart + (newL2Page * PAGE_SIZE));
    for ( uint32_t i = 0; i < PAGE_DIR_ENTRIES; i++ ){
        l2pageTable[i] = 0;
    }
}

CCPUKiddo::~CCPUKiddo() {
    for ( uint32_t i = 0; i < 1024; i++ ){
        if ( this ->  rootPageTable[i] != 0 ) {
            uint32_t l2PageNumber = this->rootPageTable[i] >> OFFSET_BITS;
            uint32_t * l2pageTable = (uint32_t *)(this -> m_MemStart + (l2PageNumber* PAGE_SIZE));
            for ( uint32_t j = 0; j < 1024; j++ ){
                if (l2pageTable[j] != 0){
                    uint32_t dataPage = l2pageTable[j] >> OFFSET_BITS;
                    stack->push(dataPage); // getting rid of contents of L2
                }
                l2pageTable[j] = 0;
            }
            stack->push(l2PageNumber); //getting rid of individual L2
        }
    }
    uint32_t newlyUnusedNumber = this->m_PageTableRoot >> OFFSET_BITS;
    stack->push(newlyUnusedNumber);
}

bool CCPUKiddo::MakeBigger(uint32_t pages) {
    uint32_t difference = pages - this -> currentMemLimit;
    this -> currentMemLimit = pages;
    if ( difference > stack->getSize() ) {
        return false; }
    uint32_t firstL1UnusedRow = 1024; // bigger than 1023 means it wont fit in the table
    uint32_t lastL1UsedRow = 1024;
    for ( uint32_t i = 0; i < 1024; i++ ){
        if ( this -> rootPageTable[i] == 0 ) {
            firstL1UnusedRow = i;
            break;
        }
        lastL1UsedRow = i;
    }

    uint32_t l2PageNumber = this->rootPageTable[lastL1UsedRow] >> OFFSET_BITS;
    uint32_t * l2pageTable = (uint32_t *)(this -> m_MemStart + (l2PageNumber * PAGE_SIZE));
    uint32_t firstL2UnusedRow = 1024;
    for ( uint32_t j = 0; j < PAGE_DIR_ENTRIES; j++ ){
        if( l2pageTable[j] == 0){
            firstL2UnusedRow = j;
            break;
        }
    }
    while(difference > 0){
        if ( firstL2UnusedRow == 1024 && firstL1UnusedRow == 1024 ){
            return false;
        }
        else if ( firstL2UnusedRow == 1024 && firstL1UnusedRow < 1024 ){
            uint32_t newL2Page = stack->pop();
            this -> rootPageTable[firstL1UnusedRow] = (newL2Page << OFFSET_BITS) | BIT_USER | BIT_WRITE | BIT_PRESENT;
            l2pageTable = (uint32_t *)(this -> m_MemStart + (newL2Page * PAGE_SIZE));
            firstL1UnusedRow++;
            firstL2UnusedRow = 0;
            for ( uint32_t i = 0; i < PAGE_DIR_ENTRIES; i++ ){
                l2pageTable[i] = 0;
            }
        }
        uint32_t newDataPage = stack->pop();
        l2pageTable[firstL2UnusedRow] = (newDataPage << OFFSET_BITS) | BIT_USER | BIT_WRITE | BIT_PRESENT;
        firstL2UnusedRow++;
        difference--;
    }
    return true;
}

bool CCPUKiddo::MakeSmaller(uint32_t pages) {
    uint32_t difference = this -> currentMemLimit - pages;
    this->currentMemLimit = pages;
    uint32_t lastL1UsedRow = 1024;
    for ( uint32_t i = 0; i < 1024; i++ ){
        if ( this -> rootPageTable[i] == 0 ) break;
        lastL1UsedRow = i;
    }
    if (lastL1UsedRow == 1024 ) return false;
    uint32_t l2PageNumber = this->rootPageTable[lastL1UsedRow] >> OFFSET_BITS;
    uint32_t * l2pageTable = (uint32_t *)(this -> m_MemStart + (l2PageNumber* PAGE_SIZE));
    uint32_t lastL2UsedRow = 1024;
    for ( uint32_t j = 0; j < PAGE_DIR_ENTRIES; j++ ){
        if( l2pageTable[j] == 0) break;
        lastL2UsedRow = j;
    }

    while ( difference > 0 ){
        uint32_t lastUsedPageNumber = l2pageTable[lastL2UsedRow] >> OFFSET_BITS;
        l2pageTable[lastL2UsedRow] = 0;
        stack->push(lastUsedPageNumber);
        if ( lastL2UsedRow > 0 ) {
            lastL2UsedRow--;
        }
        else if ( lastL2UsedRow == 0 ){
            lastL2UsedRow = 1023;
            uint32_t lastL1Number = this->rootPageTable[lastL1UsedRow] >> OFFSET_BITS;
            this->rootPageTable[lastL1UsedRow] = 0;
            stack->push(lastL1Number);
            if ( lastL1UsedRow > 0 ){
                lastL1UsedRow--;
                l2PageNumber = this->rootPageTable[lastL1UsedRow] >> OFFSET_BITS;
                l2pageTable = (uint32_t *)(this -> m_MemStart + (l2PageNumber * PAGE_SIZE));
            }
            else if ( lastL1UsedRow == 0 and difference > 1 ) return false;
            else return true;
        }
        difference--;
    }
    return true;
}

void CCPUKiddo::printTable() {
    uint32_t counterL1 = 0;
    for ( uint32_t i = 0; i < 1024; i++ ){
        if ( this->rootPageTable[i] != 0) {
            counterL1++;
            uint32_t counterL2 = 0;
            //printf("polozka root table [%d]: %d\n", i, this->rootPageTable[i]);
            uint32_t pageNumber = this->rootPageTable[i] >> OFFSET_BITS;
            uint32_t * l2pageTable = (uint32_t *)(this -> m_MemStart + (pageNumber * PAGE_SIZE));
            //printf("to je cislo stranky %d\n", pageNumber);
           //printf("obsah L2 stranky:\n");
            for ( uint32_t j = 0; j < 1024; j++ ){
                if ( l2pageTable[j] != 0 ){
                    counterL2++;
                    //printf("polozka l2 table [%d]: %d\n", j, l2pageTable[j]);
                    //uint32_t pageNumber2 = l2pageTable[j] >> OFFSET_BITS;
                    //printf("to je cislo stranky %d\n", pageNumber2);
                }
            }
            printf("L1[%d] ma %d polozek\n", i, counterL2);
        }
    }
    printf("L1 ma %d polozek", counterL1);
}


void               MemMgr                                  ( void            * mem,
                                                             uint32_t          totalPages,
                                                             void            * processArg,
                                                             void           (* mainProcess) ( CCPU *, void * ))
{
    stack = new MyStack();

    for ( uint32_t i = 0; i < totalPages; i++ ){
        stack->push(i);
    }
    pthread_mutex_init(&rpMtx, NULL);
    pthread_cond_init(&condVar, NULL);
    uint32_t rootPage = stack->pop(); // zaroven i popne
    CCPU  * ptr = new CCPUKiddo((uint8_t *)mem, rootPage*4096);
    pthread_mutex_lock(&rpMtx);
    running_processes++;
    pthread_mutex_unlock(&rpMtx);
    mainProcess(ptr,processArg);
    delete ptr;
    pthread_mutex_lock(&rpMtx);
    running_processes--;
    pthread_mutex_unlock(&rpMtx);
    pthread_mutex_lock(&rpMtx);
    while(running_processes != 0){
        pthread_cond_wait(&condVar, &rpMtx);
    }
    pthread_mutex_unlock(&rpMtx);
    delete stack;
    pthread_mutex_destroy(&rpMtx);
    pthread_cond_destroy(&condVar);
}
