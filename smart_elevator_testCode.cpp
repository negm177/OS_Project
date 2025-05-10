#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <optional>
#include <string>
#include <memory>
#include <ctime>
#include <cstdlib>

using namespace std;

// Forward declarations
class Building;
class Elevator;

// Request structure
struct Request {
    int sourceFloor;
    int destFloor;
    chrono::steady_clock::time_point timestamp;
};

// Elevator class declaration
class Elevator {
private:
    int id;
    int currentFloor = 1;
    thread thr;
    Building* building;
    bool running = true;

    static mutex logMtx;

    void log(const string& msg);
    void moveTo(int target);
    void process(const Request& r);
    void run();

public:
    Elevator(int id, Building* b);
    ~Elevator(); // ensure proper cleanup
    void start();
    void stop();
};

mutex Elevator::logMtx;

// Building class definition
class Building {
private:
    vector<shared_ptr<Elevator>> elevators;
    queue<Request> requestQ;
    mutex mtx;
    condition_variable cv;
    bool acceptingRequests = true;
    int numFloors;

public:
    Building(int numElev, int floors) : numFloors(floors) {
        for (int i = 0; i < numElev; i++) {
            elevators.push_back(make_shared<Elevator>(i, this));
        }
    }

    void startElevators() {
        for (auto& e : elevators) {
            e->start();
        }
    }

    void waitForElevators() {
        for (auto& e : elevators) {
            e->stop();
        }
    }

    void addRequest(const Request& r) {
        {
            lock_guard<mutex> lk(mtx);
            requestQ.push(r);
        }
        cv.notify_one();
    }

    void stopAcceptingRequests() {
        {
            lock_guard<mutex> lk(mtx);
            acceptingRequests = false;
        }
        cv.notify_all();
    }

    optional<Request> waitForRequest() {
        unique_lock<mutex> lk(mtx);
        cv.wait(lk, [&] { return !requestQ.empty() || !acceptingRequests; });

        if (!requestQ.empty()) {
            Request r = requestQ.front();
            requestQ.pop();
            return r;
        }
        else {
            return nullopt;
        }
    }
};

// Elevator member function definitions
void Elevator::log(const string& msg) {
    lock_guard<mutex> lk(logMtx);
    cout << "[E" << id << "] " << msg << endl;
}

void Elevator::moveTo(int target) {
    while (currentFloor != target) {
        this_thread::sleep_for(chrono::milliseconds(200));
        currentFloor += (target > currentFloor) ? 1 : -1;
        log("Passing floor " + to_string(currentFloor));
    }
}

void Elevator::process(const Request& r) {
    moveTo(r.sourceFloor);
    log("Pick up at " + to_string(r.sourceFloor));
    moveTo(r.destFloor);
    log("Drop off at " + to_string(r.destFloor));
    auto end = chrono::steady_clock::now();
    auto ms = chrono::duration_cast<chrono::milliseconds>(end - r.timestamp).count();
    log("Request time: " + to_string(ms) + " ms");
}

void Elevator::run() {
    while (running) {
        auto req = building->waitForRequest();
        if (!req) break;
        process(*req);
    }
}

Elevator::Elevator(int id, Building* b) : id(id), building(b) {}

Elevator::~Elevator() {
    if (thr.joinable()) {
        thr.join();
    }
}

void Elevator::start() {
    thr = thread(&Elevator::run, this);
}

void Elevator::stop() {
    running = false;
    building->stopAcceptingRequests(); // Ensure wake up
    if (thr.joinable()) {
        thr.join();
    }
}

// Request generator
void requestGenerator(Building& b, int numRequests, int maxFloor) {
    for (int i = 0; i < numRequests; ++i) {
        int source = rand() % maxFloor + 1;
        int dest = rand() % maxFloor + 1;
        while (dest == source) dest = rand() % maxFloor + 1;
        Request r = { source, dest, chrono::steady_clock::now() };
        b.addRequest(r);
        this_thread::sleep_for(chrono::seconds(1));
    }
    b.stopAcceptingRequests();
}

int main() {
    srand(static_cast<unsigned int>(time(nullptr)));
    const int numElevators = 2;
    const int numFloors = 10;
    const int numRequests = 10;

    Building b(numElevators, numFloors);
    b.startElevators();

    thread gen(requestGenerator, ref(b), numRequests, numFloors);
    gen.join();

    b.waitForElevators();

    cout << "Simulation completed." << endl;
    return 0;
}
