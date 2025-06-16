#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <condition_variable>
#include <atomic>
#include <map>

// Интерфейс метрики
class IMetric
{
public:
    virtual ~IMetric() = default;
    virtual std::string name() const = 0;
    virtual std::string value_string() const = 0;
    virtual void reset() = 0;
};

// Счётчик (целочисленная метрика)
class CounterMetric : public IMetric
{
public:
    CounterMetric(const std::string &name) : name_(name), value_(0) {}
    void increment(int v = 1)
    {
        value_.fetch_add(v, std::memory_order_relaxed);
    }
    int get() const
    {
        return value_.load(std::memory_order_relaxed);
    }
    std::string name() const override { return name_; }
    std::string value_string() const override { return std::to_string(get()); }
    void reset() override { value_.store(0, std::memory_order_relaxed); }

private:
    std::string name_;
    std::atomic<int> value_;
};

// Метрика для усреднения (например, CPU)
class AverageMetric : public IMetric
{
public:
    AverageMetric(const std::string &name) : name_(name), sum_(0.0), count_(0) {}
    void add(double v)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sum_ += v;
        count_ += 1;
    }
    double get_average() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ ? (sum_ / count_) : 0.0;
    }
    std::string name() const override { return name_; }
    std::string value_string() const override
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << get_average();
        return oss.str();
    }
    void reset() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sum_ = 0.0;
        count_ = 0;
    }

private:
    std::string name_;
    mutable std::mutex mutex_;
    double sum_;
    int count_;
};

// Реестр метрик и асинхронная запись в файл
class MetricsRegistry
{
public:
    MetricsRegistry(const std::string &filename, int flush_period_ms = 1000)
        : filename_(filename), flush_period_ms_(flush_period_ms), stop_flag_(false)
    {
        worker_ = std::thread([this]
                              { this->worker_func(); });
    }
    ~MetricsRegistry()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_flag_ = true;
            cond_.notify_one();
        }
        if (worker_.joinable())
            worker_.join();
    }

    // Регистрация метрики (шаблон для любых наследников IMetric)
    template <typename MetricType, typename... Args>
    std::shared_ptr<MetricType> register_metric(const std::string &name, Args &&...args)
    {
        auto metric = std::make_shared<MetricType>(name, std::forward<Args>(args)...);
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.push_back(metric);
        return metric;
    }

    void flush_now()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        flush_locked();
    }

private:
    void worker_func()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stop_flag_)
        {
            cond_.wait_for(lock, std::chrono::milliseconds(flush_period_ms_));
            if (stop_flag_)
                break;
            flush_locked();
        }
    }

    void flush_locked()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
            << "." << std::setw(3) << std::setfill('0') << ms.count();

        for (auto &metric : metrics_)
        {
            oss << " \"" << metric->name() << "\" " << metric->value_string();
        }
        oss << "\n";

        {
            std::ofstream file(filename_, std::ios::app);
            file << oss.str();
        }

        for (auto &metric : metrics_)
        {
            metric->reset();
        }
    }

    std::string filename_;
    int flush_period_ms_;
    std::vector<std::shared_ptr<IMetric>> metrics_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::thread worker_;
    bool stop_flag_;
};