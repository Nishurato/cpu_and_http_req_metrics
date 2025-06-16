#include "metrics.h"
#include <thread>
#include <random>
#include <iostream>
#include <chrono>

int main()
{
    MetricsRegistry registry("metrics.log", 1000);
    unsigned int cores = std::thread::hardware_concurrency();
    std::cout << "Cores: " << cores << std::endl;

    // Средняя утилизация CPU
    auto cpu = registry.register_metric<AverageMetric>("CPU");
    // Количество HTTP запросов
    auto http_rps = registry.register_metric<CounterMetric>("HTTP requests RPS");

    std::thread cpu_thread([&]()
                           {
        std::default_random_engine eng{std::random_device{}()};
        std::uniform_real_distribution<double> load(0.0, (double)cores);
        for (int i = 0; i < 10; ++i) {
            cpu->add(load(eng));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } });

    std::thread http_thread([&]()
                            {
        std::default_random_engine eng{std::random_device{}()};
        std::uniform_int_distribution<int> reqs(0, 100);
        for (int i = 0; i < 10; ++i) {
            http_rps->increment(reqs(eng));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } });

    cpu_thread.join();
    http_thread.join();

    registry.flush_now();

    std::cout << "Metrics logging finished. See metrics.log." << std::endl;
    return 0;
}