#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>

using namespace std;

class Data {
    int data;
public:
    Data(const int& d = 0) { data = d; }
    Data(const Data& obj) = default;
    ~Data() = default;

    int getData() const { return data; }
    void setData(const int& d) { data = d; }
    void printData() const { cout << "Data value: " << data << endl; }

    void processData(int increment) {
        int old_value = data;
        data += increment;
        cout << "Data processed: " << old_value << " + " << increment << " = " << data << endl;
    }
};

class Monitor {
    //критерий проверки наступления события
    bool dataReady;

    //несериализуемые данные 
    Data* sharedData;

    // инструмент блокировки участков кода
    mutex mtx;

    //условная переменная для ожидания событий
    condition_variable cv;

    // флаг остановки монитора
    bool stopRequested;

    // счетчики для статистики
    int providedCount;
    int consumedCount;

public:
    Monitor() {
        dataReady = false;
        sharedData = nullptr;
        stopRequested = false;
        providedCount = 0;
        consumedCount = 0;
    }

    // функция-поставщик
    void provideData(Data* data) {
        unique_lock<mutex> lock(mtx);

        //ждем пока потребитель обработает предыдущие данные
        while (dataReady && !stopRequested) {
            cout << "PROVIDER: Waiting for consumer to process previous data..." << endl;
            cv.wait(lock);
        }

        if (stopRequested) {
            cout << "PROVIDER: Monitor stopped, exiting..." << endl;
            return;
        }

        //устанавливаем новые данные
        sharedData = data;
        dataReady = true;
        providedCount++;

        cout << "PROVIDER: New data provided (total: " << providedCount << ")" << endl;
        cv.notify_one();
    }

    // функция-потребитель
    void consumeData() {
        while (true) {
            unique_lock<mutex> lock(mtx);
            while (!dataReady && !stopRequested) {
                cout << "CONSUMER: Waiting for data..." << endl;
                cv.wait(lock);
            }

            if (stopRequested && !dataReady) {
                cout << "CONSUMER: Monitor stopped, exiting..." << endl;
                break;
            }

            //обрабатываем данные
            if (sharedData != nullptr) {
                cout << "CONSUMER: Processing data - ";
                sharedData->printData();

                // имитация обработки
                sharedData->processData(5);

                consumedCount++;
                cout << "CONSUMER: Data consumed (total: " << consumedCount << ")" << endl;
            }

            dataReady = false;
            sharedData = nullptr;

            // уведомляем поставщика
            cv.notify_one();
        }
    }

    // остановка монитора
    void stopMonitor() {
        unique_lock<mutex> lock(mtx);
        stopRequested = true;
        cv.notify_all();
        cout << "MONITOR: Stop requested. Provided: " << providedCount << ", Consumed: " << consumedCount << endl;
    }

    bool isStopped() const { return stopRequested; }
    int getProvidedCount() const { return providedCount; }
    int getConsumedCount() const { return consumedCount; }
};

// функция для потока-поставщика
void providerThreadFunction(Monitor* monitor, Data* data, int operationsCount) {
    cout << "PROVIDER THREAD: Started, will perform " << operationsCount << " operations" << endl;

    for (int i = 0; i < operationsCount; i++) {
        //задержка между операциями
        this_thread::sleep_for(chrono::milliseconds(800));
        cout << "\n--- PROVIDER CYCLE " << (i + 1) << " ---" << endl;
        monitor->provideData(data);
    }

    // даем время потребителю обработать последние данные
    this_thread::sleep_for(chrono::milliseconds(100));
    monitor->stopMonitor();

    cout << "PROVIDER THREAD: Finished" << endl;
}

//функция для потока-потребителя
void consumerThreadFunction(Monitor* monitor) {
    cout << "CONSUMER THREAD: Started" << endl;
    monitor->consumeData();
    cout << "CONSUMER THREAD: Finished" << endl;
}

int main() {
    setlocale(LC_ALL, "Russian");
    cout << "OPERATING SYSTEMS - LAB 1: MONITORS   " << endl;
    Monitor monitor;
    Data data(50);

    const int OPERATIONS_COUNT = 4;

    cout << "Initial data value: " << data.getData() << endl;
    cout << "Operations to perform: " << OPERATIONS_COUNT << endl << endl;

    // создаем потоки
    thread provider(providerThreadFunction, &monitor, &data, OPERATIONS_COUNT);
    thread consumer(consumerThreadFunction, &monitor);
    provider.join();
    consumer.join();
    cout << "FINAL RESULTS" << endl;
    cout << "Final data value: " << data.getData() << endl;
    cout << "Total provided: " << monitor.getProvidedCount() << endl;
    cout << "Total consumed: " << monitor.getConsumedCount() << endl;
    return 0;
}