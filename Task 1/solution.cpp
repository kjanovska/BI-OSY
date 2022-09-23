#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"
using namespace std;
#endif /* __PROGTEST__ */

struct ShipNCargo{
    AShip ship;
    vector<CCargo> cargo;
    bool end;
};

class BufferShips{
private:
    deque<ShipNCargo *> buffer;
    mutex mtx; // mutex pro hlidani mojeho bufferu lodi, ktery je spolecny pro vsechny vlakna
    condition_variable full_buffer; // podminene promenne
    condition_variable empty_buffer;
public:
    virtual void insert (ShipNCargo * ship);
    virtual ShipNCargo * remove ( ); // remove znamena pop z fronty
};

void BufferShips::insert(ShipNCargo * ship) {
unique_lock<mutex> ulock(this->mtx);
full_buffer.wait(ulock, [this](){return sizeof(std::deque<AShip *>) + sizeof(AShip *)*buffer.size() < 400000000;});
this->buffer.emplace_back(ship);
empty_buffer.notify_all();
}

ShipNCargo * BufferShips::remove( ) {
    // samotne vybrani ( a vlozeni ) je to, na co bude nutne cekat, pak uz se to priradi do promenne a stane se z toho majetek vlakna
    // mezivlaknova je jenom prace s bufferem, ne s lodi samotnou
    ShipNCargo * ship;
    unique_lock<mutex> ulock(this->mtx);
    empty_buffer.wait(ulock, [this](){ return !this->buffer.empty();});
    ship = this->buffer.front();
    this->buffer.pop_front();

    full_buffer.notify_all();
    return ship;
}

class CCargoPlanner
{
private:
    BufferShips salesBuffer;
    BufferShips workersBuffer;
    vector<thread> salesThreads;
    vector<thread> workersThreads;
    vector<ACustomer> customers;
    int runningSales;
    int runningWorkers;
    mutex runningSalesMTX;
    mutex runningWorkersMTX;
public:
    CCargoPlanner ();
    static int               SeqSolver                     ( const vector<CCargo> & cargo,
                                                             int               maxWeight,
                                                             int               maxVolume,
                                                             vector<CCargo>  & load );
    void                     Start                         ( int               sales,
                                                             int               workers );
    void                     Stop                          ( );

    void                     Customer                      ( ACustomer         customer );
    void                     Ship                          ( AShip             ship );
    void                     SalesFunction                 ( );
    void                     WorkersFunction               ( );
};

int CCargoPlanner::SeqSolver(const vector<CCargo> &cargo, int maxWeight, int maxVolume, vector<CCargo> &load) {
    return ProgtestSolver(cargo, maxWeight, maxVolume, load);
}

void CCargoPlanner::Start(int sales, int workers) {
// inicializace vlaken
this -> runningSales = sales;
this -> runningWorkers = workers;
for ( int i = 0; i < sales; i ++ ){
    this->salesThreads.emplace_back(&CCargoPlanner::SalesFunction, this);
}
for ( int i = 0; i < workers; i++ ){
    this->workersThreads.emplace_back(&CCargoPlanner::WorkersFunction, this);
}
}

void CCargoPlanner::Stop() {
    // signal obchodnikum, ze uz neprijdou nove lode a maji koncit - naraznik
    int tmpSales = 0;
    unique_lock<mutex> u(this->runningSalesMTX);
    tmpSales = this -> runningSales;
    u.unlock();
    for ( int i = 0; i < tmpSales; i++ ){
        AShip tmpShip = AShip();
        vector<CCargo> emptyVector;
        auto * ship = new ShipNCargo{tmpShip, emptyVector, true};
        this -> salesBuffer.insert(ship);
    }

    // CEKANI NA DOBEHNUTI VLAKEN
    for ( auto & sale : this->salesThreads )
        sale.join ();
    for ( auto & worker : this->workersThreads )
        worker.join ();
}

void CCargoPlanner::Customer(ACustomer customer) {
// registrace zakaznika - vlozeni jej do mojeho seznamu zakazniku
this->customers.emplace_back(customer);
}

void CCargoPlanner::Ship(AShip ship) {
// predani lode k nakladce - ZARIDI PREDANI LODE VLAKNUM OBCHODNIKU A VYPOCETNIM VLAKNUM
vector<CCargo> newCargo;
auto * newShip = new ShipNCargo { ship, newCargo, false };
this->salesBuffer.insert(newShip); // tady bude kontrola plnosti bufferu
}

void CCargoPlanner::SalesFunction() {
// salesBuffer a workersBuffer jsou spolecne uloziste, tedy musi byt obaleny mutexy
// pokud muze (kontrola v buffer funkci), vybere z salesBuffer jednu lod -> do nove promenne
// pokud je ta lod prazdna lod, tak se vlakno ukonci
// pokud muze (kontrola v buffer funkci), vlozi do workersBufferu novou lod
// konec vlaknove funkce znamena konec vlakna -> bude vybirat lodicky ve for cyklu
int rs;
while(true){
    ShipNCargo * ship = this->salesBuffer.remove();
    if ( ship->end ){
        // pokud je to prazdna lod, tak se vlakno ukonci
        delete(ship);
        break;
    }
    // projede vsechny zakazniky, pokud maji neco pro destination od lodi, tak sup do spolecneho vectoru
    for ( const auto& customer : this -> customers ){
        vector<CCargo> tmpCargo;
        customer -> Quote(ship->ship->Destination(), tmpCargo);
        for ( auto i : tmpCargo ){
            ship->cargo.emplace_back(i);
        }
    }
    // pokud muze (kontrola v buffer funkci), vlozi do workersBufferu novou lod
    this -> workersBuffer.insert(ship);
}
    unique_lock<mutex> ulock(this->runningSalesMTX);
    this->runningSales--;
    rs = this -> runningSales;
    ulock.unlock();

    if ( rs == 0 ){
        for ( int i = 0; i < this->runningWorkers; i++){
            AShip tmpShip = AShip();
            vector<CCargo> emptyVector;
            auto * ship = new ShipNCargo{tmpShip, emptyVector, true};
            this -> workersBuffer.insert(ship);
        }
    }
}

void CCargoPlanner::WorkersFunction() {
    // ve while cyklu vybira pokud to neni end polozka tak jede dal
    while ( true ){
        // pokud muze, vybira z workersBuffer lod -> musim tu lod nekam ulozit - je to pointer, musim si ho schovat
        ShipNCargo * ship = this->workersBuffer.remove();
        if ( ship->end ){
            delete(ship);
            // pokud je to prazdna lod, tak se vlakno ukonci
            break;
        }
        // ProgtestSolver
        vector<CCargo> finalCargo;
        this -> SeqSolver(ship->cargo, ship->ship->MaxWeight(), ship->ship->MaxVolume(), finalCargo);
        // Load
        ship->ship->Load(finalCargo);
        delete(ship);
    }
    unique_lock<mutex> ulock(this->runningWorkersMTX);
    this->runningWorkers--;
    ulock.unlock();
}

CCargoPlanner::CCargoPlanner() {
    this->runningSales = 0;
    this->runningWorkers = 0;
}

//-------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__
int                main                                    ( void )
{
  CCargoPlanner  test;
  vector<AShipTest> ships;
  vector<ACustomerTest> customers { make_shared<CCustomerTest> (), make_shared<CCustomerTest> () };
  
  ships . push_back ( g_TestExtra[0] . PrepareTest ( "New York", customers ) );
  ships . push_back ( g_TestExtra[1] . PrepareTest ( "Barcelona", customers ) );
  ships . push_back ( g_TestExtra[2] . PrepareTest ( "Kobe", customers ) );
  ships . push_back ( g_TestExtra[8] . PrepareTest ( "Perth", customers ) );
  // add more ships here
  
  for ( auto x : customers )
    test . Customer ( x );
  
  test . Start ( 3, 2 );
  
  for ( auto x : ships )
    test . Ship ( x );

  test . Stop  ();

  for ( auto x : ships )
    cout << x -> Destination () << ": " << ( x -> Validate () ? "ok" : "fail" ) << endl;
  return 0;  
}
#endif /* __PROGTEST__ */ 
