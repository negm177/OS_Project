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

// Forward declarations
class Building;
class Elevator;

// Request structure
struct Request {
    int sourceFloor;
    int destFloor;
    std::chrono::steady_clock::time_point timestamp;
};

// Elevator class declaration
class Elevator {
private:
    int id;
    int currentFloor = 1;
    std::thread thr;
    Building* building;
    bool running = true;

    static std::mutex logMtx;

    void log(const std::string& msg);
    void moveTo(int target);
    void process(const Request& r);
    void run();

public:
    Elevator(int id, Building* b);
    ~Elevator(); // ensure proper cleanup
    void start();
    void stop();
};

std::mutex Elevator::logMtx;

// Building class definition
class Building {
private:
    std::vector<std::shared_ptr<Elevator>> elevators;
    std::queue<Request> requestQ;
    std::mutex mtx;
    std::condition_variable cv;
    bool acceptingRequests = true;
    int numFloors;

public:
    Building(int numElev, int floors) : numFloors(floors) {
        for (int i = 0; i < numElev; i++) {
            elevators.push_back(std::make_shared<Elevator>(i, this));
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
            std::lock_guard<std::mutex> lk(mtx);
            requestQ.push(r);
        }
        cv.notify_one();
    }

    void stopAcceptingRequests() {
        {
            std::lock_guard<std::mutex> lk(mtx);
            acceptingRequests = false;
        }
        cv.notify_all();
    }

    std::optional<Request> waitForRequest() {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [&] { return !requestQ.empty() || !acceptingRequests; });

        if (!requestQ.empty()) {
            Request r = requestQ.front();
            requestQ.pop();
            return r;
        }
        else {
            return std::nullopt;
        }
    }
};

// Elevator member function definitions
void Elevator::log(const std::string& msg) {
    std::lock_guard<std::mutex> lk(logMtx);
    std::cout << "[E" << id << "] " << msg << std::endl;
}

void Elevator::moveTo(int target) {
    while (currentFloor != target) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentFloor += (target > currentFloor) ? 1 : -1;
        log("Passing floor " + std::to_string(currentFloor));
    }
}

void Elevator::process(const Request& r) {
    moveTo(r.sourceFloor);
    log("Pick up at " + std::to_string(r.sourceFloor));
    moveTo(r.destFloor);
    log("Drop off at " + std::to_string(r.destFloor));
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - r.timestamp).count();
    log("Request time: " + std::to_string(ms) + " ms");
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
    thr = std::thread(&Elevator::run, this);
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
        int source = std::rand() % maxFloor + 1;
        int dest = std::rand() % maxFloor + 1;
        while (dest == source) dest = std::rand() % maxFloor + 1;
        Request r = { source, dest, std::chrono::steady_clock::now() };
        b.addRequest(r);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    b.stopAcceptingRequests();
}

int main() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    const int numElevators = 2;
    const int numFloors = 10;
    const int numRequests = 10;

    Building b(numElevators, numFloors);
    b.startElevators();

    std::thread gen(requestGenerator, std::ref(b), numRequests, numFloors);
    gen.join();

    b.waitForElevators();

    std::cout << "Simulation completed." << std::endl;
    return 0;
}
